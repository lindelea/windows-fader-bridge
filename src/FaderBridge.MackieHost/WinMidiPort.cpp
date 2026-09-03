#include "WinMidiPort.h"
#include "DiagnosticLog.h"

namespace
{
// Feedback is state, not an event stream. Replacing an unsent value prevents a
// slow driver from building a stale motor/meter backlog while preserving the
// latest state for every independent control.
std::uint32_t FeedbackKey(const mackie::Bytes& bytes)
{
    if (bytes.empty() || bytes[0] == 0xF0) return UINT32_MAX;
    const auto status = bytes[0];
    if ((status & 0xF0) == 0xE0) return 0x10000U | status;
    if ((status & 0xF0) == 0x90 && bytes.size() >= 2) return 0x20000U | (status << 8) | bytes[1];
    if ((status & 0xF0) == 0xB0 && bytes.size() >= 2) return 0x30000U | (status << 8) | bytes[1];
    if ((status & 0xF0) == 0xD0 && bytes.size() >= 2)
    {
        const auto channel = bytes[1] >> 4;
        const auto level = bytes[1] & 0x0F;
        return 0x40000U | (channel << 1) | (level >= 14 ? 1U : 0U);
    }
    return UINT32_MAX;
}
}

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
    { const std::scoped_lock lock(statusMutex_);
      error_ = std::wstring(operation) + L": " + text + L" (" + std::to_wstring(result) + L")"; }
    ++Errors;
    FB_TRACE("MIDI_ERROR operation=%ls code=%u", operation, result);
    return false;
}
std::wstring WinMidiPort::Error() const
{
    const std::scoped_lock lock(statusMutex_);
    return error_;
}
WinMidiPort::~WinMidiPort() { Close(); }
bool WinMidiPort::Open(UINT input, UINT output)
{
    Close();
    faulted_.store(false, std::memory_order_release);
    { const std::scoped_lock lock(statusMutex_); error_.clear(); }
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WindowsFaderBridge.Mackie.MidiReceiver";
    RegisterClassW(&wc);
    window_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, wc.hInstance, this);
    if (!window_) { const std::scoped_lock lock(statusMutex_); error_ = L"Cannot create MIDI message receiver"; return false; }
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
    {
        const std::scoped_lock lock(outputMutex_);
        outputQueue_.clear();
        outputRunning_ = true;
    }
    try { outputThread_ = std::thread(&WinMidiPort::OutputLoop, this); }
    catch (...) { { const std::scoped_lock lock(outputMutex_); outputRunning_ = false; } Close(); return false; }
    FB_TRACE("MIDI_CONNECTED input=%u output=%u", input, output);
    return true;
}
void WinMidiPort::Collect()
{
    // Output buffers belong exclusively to OutputLoop. Retained for the host's
    // existing lifecycle call site; collection now happens off the input/UI thread.
}
void WinMidiPort::CollectOutput()
{
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
    if (!out_ || closing_ || faulted_.load(std::memory_order_acquire) || bytes.empty()) return;
    if (bytes[0] == 0xF0)
    {
        if (bytes.back() != 0xF7 || bytes.size() > 1024) return;
    }
    else
    {
        if (bytes.size() < 2 || bytes.size() > 3) return;
        const DWORD packed = bytes[0] | (DWORD(bytes[1]) << 8) |
            (bytes.size() > 2 ? DWORD(bytes[2]) << 16 : 0);
        if (!mackie::ValidShort(packed)) return;
    }
    {
        const std::scoped_lock lock(outputMutex_);
        if (!outputRunning_) return;
        const auto key = FeedbackKey(bytes);
        if (key != UINT32_MAX)
        {
            const auto pending = std::find_if(outputQueue_.begin(), outputQueue_.end(),
                [&](const auto& queued) { return FeedbackKey(queued) == key; });
            if (pending != outputQueue_.end()) { *pending = bytes; outputWake_.notify_one(); return; }
        }
        if (outputQueue_.size() >= 512)
        {
            { const std::scoped_lock statusLock(statusMutex_); error_ = L"MIDI output queue stalled; reconnect to resync"; }
            ++Errors; faulted_.store(true, std::memory_order_release); return;
        }
        outputQueue_.push_back(bytes);
    }
    outputWake_.notify_one();
}
void WinMidiPort::SendNow(const mackie::Bytes& bytes)
{
    if (bytes[0] != 0xF0)
    {
        DWORD packed = bytes[0] | (DWORD(bytes[1]) << 8);
        if (bytes.size() > 2) packed |= DWORD(bytes[2]) << 16;
        if (!Check(midiOutShortMsg(out_, packed), L"Send short")) { faulted_.store(true, std::memory_order_release); return; }
        if (Trace.load(std::memory_order_relaxed)) FB_TRACE("MIDI_TX short=%06lX", packed);
    }
    else
    {
        CollectOutput();
        if (outputBuffers_.size() >= 64)
        {
            { const std::scoped_lock lock(statusMutex_); error_ = L"MIDI output stalled; disconnect/reconnect to resync"; }
            ++Errors; faulted_.store(true, std::memory_order_release); return;
        }
        auto buffer = std::make_unique<OutBuffer>();
        buffer->bytes.assign(bytes.begin(), bytes.end());
        buffer->header.lpData = buffer->bytes.data();
        buffer->header.dwBufferLength = static_cast<DWORD>(buffer->bytes.size());
        if (!Check(midiOutPrepareHeader(out_, &buffer->header, sizeof(MIDIHDR)), L"Prepare output")) { faulted_.store(true, std::memory_order_release); return; }
        auto* header = &buffer->header;
        outputBuffers_.push_back(std::move(buffer));
        if (!Check(midiOutLongMsg(out_, header, sizeof(MIDIHDR)), L"Send SysEx"))
        {
            header->dwFlags |= MHDR_DONE;
            faulted_.store(true, std::memory_order_release);
            CollectOutput();
            return;
        }
        if (Trace.load(std::memory_order_relaxed)) FB_TRACE("MIDI_TX sysex_len=%zu command=%02X offset=%02X", bytes.size(),
            bytes.size() > 5 ? bytes[5] : 0, bytes.size() > 6 ? bytes[6] : 0);
    }
    ++Sent;
}
void WinMidiPort::OutputLoop()
{
    for (;;)
    {
        mackie::Bytes bytes;
        {
            std::unique_lock lock(outputMutex_);
            outputWake_.wait_for(lock, std::chrono::milliseconds(20), [&] { return !outputRunning_ || !outputQueue_.empty(); });
            if (!outputRunning_) break;
            if (!outputQueue_.empty()) { bytes = std::move(outputQueue_.front()); outputQueue_.pop_front(); }
        }
        if (!bytes.empty()) SendNow(bytes);
        else CollectOutput();
    }
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
    {
        const std::scoped_lock lock(outputMutex_);
        outputRunning_ = false;
        outputQueue_.clear();
    }
    outputWake_.notify_all();
    if (outputThread_.joinable()) outputThread_.join();
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
        if (Trace.load(std::memory_order_relaxed)) FB_TRACE("MIDI_RX short=%06lX", static_cast<DWORD>(value));
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
    else if (message == MM_MIM_CLOSE) { faulted_.store(true, std::memory_order_release); ++Errors; const std::scoped_lock lock(statusMutex_); error_ = L"MIDI input closed by driver"; }
    return 0;
}
