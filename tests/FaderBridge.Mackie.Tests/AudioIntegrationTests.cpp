#include "NativeAudioController.h"
#include "MackieSurface.h"
#include "DiagnosticLog.h"
#include <Audioclient.h>
#include <Audiopolicy.h>
#include <Mmdeviceapi.h>
#include <wrl/client.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <clocale>
using Microsoft::WRL::ComPtr;
namespace
{
constexpr UINT Snapshot = WM_APP + 42;
void Hr(HRESULT result, const char* operation) { if (FAILED(result)) { std::cerr << operation << " HRESULT=" << std::hex << result << '\n'; throw std::runtime_error(operation); } }
void Check(bool okay, const char* operation) { if (!okay) throw std::runtime_error(operation); }
struct Fixture
{
    HWND window = nullptr;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    ComPtr<ISimpleAudioVolume> volume;
    ComPtr<IChannelAudioVolume> channels;
    std::unique_ptr<NativeAudioController> controller;
    std::unique_ptr<AudioFrame> frame;
    std::vector<mackie::Bytes> feedback;
    std::wstring key;
    UINT32 capacity = 0;
    mackie::Surface surface{[&](const auto& bytes) { feedback.push_back(bytes); }, [&](const mackie::Action& action) {
        // The test can ONLY target the silent session owned by this process.
        Check(!key.empty() && action.key == key, "integration action escaped test session");
        using K = mackie::ActionKind;
        const auto kind = action.kind == K::Volume ? AudioTrackControl::Volume :
            action.kind == K::Pan ? AudioTrackControl::Pan : AudioTrackControl::Mute;
        Check(action.kind == K::Volume || action.kind == K::Pan || action.kind == K::Mute, "unsafe integration action");
        Check(controller->QueueTrackControl(kind, key, action.value), "queue integration action");
    }};
    Fixture()
    {
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"Wfb.Mackie.AudioIntegration";
        RegisterClassW(&wc);
        window = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);
        Check(window != nullptr, "create integration receiver");
        ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDevice> endpoint;
        Hr(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)), "enumerator");
        Hr(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &endpoint), "read default output");
        Hr(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client), "activate silent stream");
        WAVEFORMATEX* format = nullptr; Hr(client->GetMixFormat(&format), "read mix format");
        GUID session{}; CoCreateGuid(&session);
        const auto initialized = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, 1000000, 0, format, &session);
        CoTaskMemFree(format); Hr(initialized, "initialize nonpersistent shared stream");
        Hr(client->GetService(IID_PPV_ARGS(&render)), "render service");
        Hr(client->GetService(IID_PPV_ARGS(&volume)), "test session volume service");
        Hr(client->GetService(IID_PPV_ARGS(&channels)), "test session balance service");
        Hr(client->GetBufferSize(&capacity), "buffer size"); FillSilence(); Hr(client->Start(), "start silent session");
        controller = std::make_unique<NativeAudioController>(window, Snapshot); controller->Start();
    }
    ~Fixture()
    {
        controller.reset(); if (client) client->Stop();
        MSG message{};
        while (PeekMessageW(&message, window, Snapshot, Snapshot, PM_REMOVE)) delete reinterpret_cast<AudioFrame*>(message.lParam);
        if (window) DestroyWindow(window);
    }
    const AudioStripState* Own() const
    {
        if (frame) for (const auto& strip : frame->strips) if (strip.active && strip.role == AudioStripRole::Application &&
            std::find(strip.focusProcessIds.begin(), strip.focusProcessIds.end(), GetCurrentProcessId()) != strip.focusProcessIds.end()) return &strip;
        return nullptr;
    }
    void FillSilence()
    {
        UINT32 padding = 0; Hr(client->GetCurrentPadding(&padding), "padding");
        if (capacity > padding)
        { BYTE* memory = nullptr; Hr(render->GetBuffer(capacity - padding, &memory), "silent buffer"); Hr(render->ReleaseBuffer(capacity - padding, AUDCLNT_BUFFERFLAGS_SILENT), "release silent buffer"); }
    }
    void Pump()
    {
        FillSilence(); MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        if (const auto strip = Own())
        {
            key = strip->key; mackie::Track t; t.key = key; t.name = strip->name; t.active = true; t.application = true;
            t.volume = strip->volume; t.pan = strip->pan; t.canPan = strip->panAvailable; t.muted = strip->muted;
            surface.Update({t}, GetTickCount64()); surface.Feedback(GetTickCount64());
        }
        Sleep(5);
    }
    void Until(const std::function<bool()>& predicate, const char* label)
    {
        const auto deadline = GetTickCount64() + 5000;
        do { Pump(); if (predicate()) { std::cout << "PASS " << label << '\n'; return; } } while (GetTickCount64() < deadline);
        throw std::runtime_error(label);
    }
    void Note(int note, bool down) { surface.Input(0x90 | (note << 8) | ((down ? 127 : 0) << 16), GetTickCount64()); }
    void Press(int note) { Note(note, true); Note(note, false); }
    bool WindowsVolume(float expected) { float actual = 0; return SUCCEEDED(volume->GetMasterVolume(&actual)) && std::abs(actual - expected) < .002F; }
    bool WindowsMute(bool expected) { BOOL actual = FALSE; return SUCCEEDED(volume->GetMute(&actual)) && (actual != FALSE) == expected; }
    static LRESULT CALLBACK Proc(HWND w, UINT m, WPARAM a, LPARAM b)
    {
        auto self = reinterpret_cast<Fixture*>(GetWindowLongPtrW(w, GWLP_USERDATA));
        if (m == WM_NCCREATE) { self = static_cast<Fixture*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams); SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        if (m == Snapshot && self) { self->frame.reset(reinterpret_cast<AudioFrame*>(b)); return 0; }
        return DefWindowProcW(w, m, a, b);
    }
};
}
int main()
{
    std::setlocale(LC_ALL, ".UTF8");
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); if (FAILED(com)) return 2;
    DiagnosticLog::Instance().Start(); int code = 0;
    try
    {
        Fixture f;
        f.Until([&] { return f.Own() != nullptr; }, "discover own silent Windows session");
        f.Note(0x68, true); const auto bytes = mackie::Fader(0, .37F);
        f.surface.Input(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16), GetTickCount64()); f.Note(0x68, false);
        f.Until([&] { return f.WindowsVolume(.37F) && std::abs(f.Own()->volume - .37F) < .002F; }, "MCU fader -> keyed queue -> actual Windows volume");
        f.surface.Input(0xB0 | (0x10 << 8) | (3 << 16), GetTickCount64());
        f.Until([&] { return f.WindowsVolume(.40F) && std::abs(f.Own()->volume - .40F) < .002F; }, "encoder 1 -> current channel Windows volume");
        f.feedback.clear(); Hr(f.volume->SetMasterVolume(.61F, nullptr), "external test session volume");
        f.Until([&] { return std::find(f.feedback.begin(), f.feedback.end(), mackie::Fader(0, .61F)) != f.feedback.end(); }, "Windows volume -> MCU motor feedback");
        Hr(f.volume->SetMute(FALSE, nullptr), "initial unmute of own silent session");
        f.Until([&] { return !f.Own()->muted; }, "read initial test session mute");
        f.Press(0x10); f.Until([&] { return f.WindowsMute(true) && f.Own()->muted; }, "MCU mute -> Windows");
        f.Press(0x10); f.Until([&] { return f.WindowsMute(false) && !f.Own()->muted; }, "MCU unmute -> Windows");
        if (f.Own()->panAvailable)
        {
            f.surface.Input(0xB0 | (0x11 << 8) | (63 << 16), GetTickCount64());
            f.Until([&] { return f.Own()->pan > .99F; }, "MCU encoder -> Windows channel balance");
            f.Press(0x21); f.Until([&] { return std::abs(f.Own()->pan) < .01F; }, "encoder push -> Windows balance center");
        }
        else std::cout << "SKIP pan: default output/session is not balance-capable\n";
        Check(f.controller->QueueTrackControl(AudioTrackControl::Volume, L"integration:nonexistent", .99F), "queue stale identity");
        const auto until = GetTickCount64() + 300; while (GetTickCount64() < until) f.Pump();
        Check(f.WindowsVolume(.61F), "stale identity must not touch live test session");
        std::cout << "PASS stale identity rejected. No MIDI opened; only this process's silent nonpersistent session was changed.\n";
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; code = 1; }
    DiagnosticLog::Instance().Stop(); CoUninitialize(); return code;
}
