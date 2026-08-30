#include "WinMidiPort.h"
#include "DiagnosticLog.h"

std::vector<MidiPortName> WinMidiPort::Inputs(bool traceEnumeration)
{
    std::vector<MidiPortName> ports;
    if (traceEnumeration) FB_TRACE("MIDI_ENUM_INPUT begin");
    const auto count = midiInGetNumDevs();
    if (traceEnumeration) FB_TRACE("MIDI_ENUM_INPUT count=%u", count);
    for (UINT i = 0; i < count; ++i)
    {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) ports.push_back({i, caps.szPname});
    }
    if (traceEnumeration) FB_TRACE("MIDI_ENUM_INPUT end");
    return ports;
}
std::vector<MidiPortName> WinMidiPort::Outputs(bool traceEnumeration)
{
    std::vector<MidiPortName> ports;
    if (traceEnumeration) FB_TRACE("MIDI_ENUM_OUTPUT begin");
    const auto count = midiOutGetNumDevs();
    if (traceEnumeration) FB_TRACE("MIDI_ENUM_OUTPUT count=%u", count);
    for (UINT i = 0; i < count; ++i)
    {
        MIDIOUTCAPSW caps{};
        if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) ports.push_back({i, caps.szPname});
    }
    if (traceEnumeration) FB_TRACE("MIDI_ENUM_OUTPUT end");
    return ports;
}
bool WinMidiPort::Check(MMRESULT result, const wchar_t* operation)
{
    if (result == MMSYSERR_NOERROR) return true;
    wchar_t text[256]{};
    midiOutGetErrorTextW(result, text, 256);
    error_ = std::wstring(operation) + L": " + text + L" (" + std::to_wstring(result) + L")";
    ++Errors;
    FB_TRACE("MIDI_ERROR operation=%ls code=%u", operation, result);
    return false;
}
WinMidiPort::~WinMidiPort() { Close(); }
bool WinMidiPort::Open(UINT input, UINT output)
{
    Close();
    faulted_ = false;
    error_.clear();
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WindowsFaderBridge.Mackie.MidiReceiver";
    RegisterClassW(&wc);
    window_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, wc.hInstance, this);
    if (!window_) { error_ = L"Cannot create MIDI message receiver"; return false; }
    if (!Check(midiOutOpen(&out_, output, 0, 0, CALLBACK_NULL), L"Open output")) { Close(); return false; }
    if (!Check(midiInOpen(&in_, input, reinterpret_cast<DWORD_PTR>(window_), 0,
        CALLBACK_WINDOW | MIDI_IO_STATUS), L"Open input")) { Close(); return false; }
    for (auto& buffer : inputBuffers_)
    {
        buffer = std::make_unique<InBuffer>();
        buffer->header.lpData = buffer->bytes.data();
        buffer->header.dwBufferLength = static_cast<DWORD>(buffer->bytes.size());
        if (!Check(midiInPrepareHeader(in_, &buffer->header, sizeof(MIDIHDR)), L"Prepare input")) { Close(); return false; }
        buffer->prepared = true;
        if (!Check(midiInAddBuffer(in_, &buffer->header, sizeof(MIDIHDR)), L"Queue input")) { Close(); return false; }
    }
    if (!Check(midiInStart(in_), L"Start input")) { Close(); return false; }
    FB_TRACE("MIDI_CONNECTED input=%u output=%u", input, output);
    return true;
}
void WinMidiPort::Collect()
{
    if (!out_) return;
    for (auto it = outputBuffers_.begin(); it != outputBuffers_.end();)
    {
        auto& header = (*it)->header;
        if ((header.dwFlags & MHDR_DONE) &&
            midiOutUnprepareHeader(out_, &header, sizeof(header)) == MMSYSERR_NOERROR)
            it = outputBuffers_.erase(it);
        else ++it;
    }
}
void WinMidiPort::Send(const mackie::Bytes& bytes)
{
    if (!out_ || closing_ || faulted_ || bytes.empty()) return;
    if (bytes[0] != 0xF0)
    {
        if (bytes.size() < 2 || bytes.size() > 3) return;
        DWORD packed = bytes[0] | (DWORD(bytes[1]) << 8);
        if (bytes.size() > 2) packed |= DWORD(bytes[2]) << 16;
        if (!mackie::ValidShort(packed)) return;
        if (!Check(midiOutShortMsg(out_, packed), L"Send short")) { faulted_ = true; return; }
        if (Trace) FB_TRACE("MIDI_TX short=%06lX", packed);
    }
    else
    {
        Collect();
        if (bytes.back() != 0xF7 || bytes.size() > 1024) return;
        if (outputBuffers_.size() >= 64)
        {
            error_ = L"MIDI output stalled; disconnect/reconnect to resync";
            ++Errors; faulted_ = true; return;
        }
        auto buffer = std::make_unique<OutBuffer>();
        buffer->bytes.assign(bytes.begin(), bytes.end());
        buffer->header.lpData = buffer->bytes.data();
        buffer->header.dwBufferLength = static_cast<DWORD>(buffer->bytes.size());
        if (!Check(midiOutPrepareHeader(out_, &buffer->header, sizeof(MIDIHDR)), L"Prepare output")) { faulted_ = true; return; }
        auto* header = &buffer->header;
        outputBuffers_.push_back(std::move(buffer));
        if (!Check(midiOutLongMsg(out_, header, sizeof(MIDIHDR)), L"Send SysEx"))
        {
            header->dwFlags |= MHDR_DONE;
            faulted_ = true;
            Collect();
            return;
        }
        if (Trace) FB_TRACE("MIDI_TX sysex_len=%zu command=%02X offset=%02X", bytes.size(),
            bytes.size() > 5 ? bytes[5] : 0, bytes.size() > 6 ? bytes[6] : 0);
    }
    ++Sent;
}
void WinMidiPort::Close()
{
    closing_ = true;
    if (in_)
    {
        Check(midiInStop(in_), L"Stop input");
        Check(midiInReset(in_), L"Reset input buffers");
        for (auto& buffer : inputBuffers_) if (buffer && buffer->prepared)
        {
            if (!Check(midiInUnprepareHeader(in_, &buffer->header, sizeof(MIDIHDR)), L"Unprepare input"))
                (void)buffer.release(); // Quarantine abnormal driver-owned memory; never free early.
        }
        Check(midiInClose(in_), L"Close input");
        in_ = nullptr;
    }
    for (auto& buffer : inputBuffers_) buffer.reset();
    if (out_)
    {
        // This WinMM buffer reset is not a device SysEx reset/firmware command.
        Check(midiOutReset(out_), L"Reset output buffers");
        for (auto& buffer : outputBuffers_)
        {
            if (!Check(midiOutUnprepareHeader(out_, &buffer->header, sizeof(MIDIHDR)), L"Unprepare output"))
            {
                // An abnormal driver must never retain a pointer into freed memory.
                // Quarantine its allocation for process lifetime; report the failure.
                (void)buffer.release();
            }
        }
        outputBuffers_.clear();
        Check(midiOutClose(out_), L"Close output");
        out_ = nullptr;
    }
    if (window_) { DestroyWindow(window_); window_ = nullptr; }
    closing_ = false;
}
LRESULT CALLBACK WinMidiPort::WindowProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto* self = reinterpret_cast<WinMidiPort*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        self = static_cast<WinMidiPort*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && message >= MM_MIM_OPEN && message <= MM_MIM_MOREDATA) return self->Message(message, w, l);
    return DefWindowProcW(window, message, w, l);
}
LRESULT WinMidiPort::Message(UINT message, WPARAM handle, LPARAM value)
{
    if (closing_ || reinterpret_cast<HMIDIIN>(handle) != in_) return 0;
    if (message == MM_MIM_DATA || message == MM_MIM_MOREDATA)
    {
        ++Received;
        if (Trace) FB_TRACE("MIDI_RX short=%06lX", static_cast<DWORD>(value));
        receiver_(static_cast<DWORD>(value));
    }
    else if (message == MM_MIM_LONGDATA || message == MM_MIM_LONGERROR)
    {
        auto* header = reinterpret_cast<MIDIHDR*>(value);
        if (!header) return 0;
        ++Received;
        FB_TRACE("MIDI_RX_SYSEX bytes=%lu error=%d (diagnostic only; no vendor/handshake response)", header->dwBytesRecorded, message == MM_MIM_LONGERROR);
        header->dwBytesRecorded = 0;
        if (!Check(midiInAddBuffer(in_, header, sizeof(MIDIHDR)), L"Requeue input")) faulted_ = true;
    }
    else if (message == MM_MIM_ERROR) { ++Errors; FB_TRACE("MIDI_RX_ERROR short=%06lX", static_cast<DWORD>(value)); }
    else if (message == MM_MIM_CLOSE) { faulted_ = true; ++Errors; error_ = L"MIDI input closed by driver"; }
    return 0;
}
