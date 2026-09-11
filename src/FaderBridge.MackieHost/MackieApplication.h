#pragma once
#include "WinMidiPort.h"
#include "MackieSurface.h"
#include "MackieSettings.h"
#include "MackieMedia.h"
#include "MackieDisplayClock.h"
#include "MackieWorkspace.h"
#include "MackieConnectionPolicy.h"
#include "../BridgeGlobalShortcut.h"
#include "NativeAudioController.h"
#include "WindowsCommand.h"
#include <CommCtrl.h>
#include <Shellapi.h>
#include <condition_variable>
#include <deque>

class MackieDesktop;
class MackieApplication final
{
public:
    explicit MackieApplication(bool smoke, bool diagnostics = false);
    ~MackieApplication();
    int Run(HINSTANCE instance, int show);
private:
    friend class MackieDesktop;
    struct DeviceContext
    {
        DeviceContext(MackieApplication&, std::string, MackieSettings);
        std::string id;
        MackieSettings settings;
        WinMidiPort midi;
        mackie::Surface surface;
        mackie::DisplayClock clock;
        std::unique_ptr<MackieMedia> media;
        std::array<bool,2048> pressed{};
        std::map<int,bool> feedback;
        mackie::ConnectionPolicy connection;
        bool dirty=false, learning=false;
        std::wstring learningCommand, mediaStatus;
        std::uint64_t saveDue=0;
        UINT inputIndex=0, outputIndex=0;
    };
    struct DeviceScope
    {
        MackieApplication& app; DeviceContext* previous;
        DeviceScope(MackieApplication& a,DeviceContext* d):app(a),previous(a.dispatch_){a.dispatch_=d;}
        ~DeviceScope(){app.dispatch_=previous;}
    };
    MackieWorkspace workspace_;
    std::vector<std::unique_ptr<DeviceContext>> devices_;
    std::unique_ptr<DeviceContext> emptyDevice_;
    DeviceContext* selectedDevice_=nullptr;
    DeviceContext* dispatch_=nullptr;
    DeviceContext& Device() const { return *(dispatch_?dispatch_:selectedDevice_?selectedDevice_:emptyDevice_.get()); }
    bool IsSelectedDevice() const { return !dispatch_||dispatch_==selectedDevice_; }
    MackieSettings& Settings() const { return Device().settings; }
    WinMidiPort& Midi() const { return Device().midi; }
    mackie::Surface& Surface() const { return Device().surface; }
    mackie::DisplayClock& DisplayClock() const { return Device().clock; }
    std::unique_ptr<MackieMedia>& Media() const { return Device().media; }
    auto& CustomPressed() const { return Device().pressed; }
    auto& CustomFeedback() const { return Device().feedback; }
    bool& DirtySettings() const { return Device().dirty; }
    bool& Learning() const { return Device().learning; }
    auto& LearningCommand() const { return Device().learningCommand; }
    auto& MediaStatus() const { return Device().mediaStatus; }
    auto& SaveDue() const { return Device().saveDue; }
    auto& SelectedInput() const { return Device().inputIndex; }
    auto& SelectedOutput() const { return Device().outputIndex; }
    bool SelectDevice(std::size_t);
    bool AddDevice();
    bool RemoveDevice();
    bool SaveDeviceConfiguration(const std::wstring&,const std::wstring&,const std::wstring&,const std::wstring&);
    std::wstring DeviceLabel(const DeviceContext&) const;
    bool OpenDevice(UINT input,UINT output);
    void ReconcileDevices();
    void SyncDeviceControls();
    void TickDevice(std::uint64_t now);
    std::unique_ptr<MackieDesktop> desktop_;
    bridge::GlobalShortcutRegistration globalShortcut_;
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EncoderWindowProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK JogWindowProc(HWND, UINT, WPARAM, LPARAM);
    void ShowJogSettings();
    void SaveJogSettings();
    void ShowEncoderSettings();
    void SaveEncoderSettings();
    LRESULT Message(UINT, WPARAM, LPARAM);
    HWND Control(const wchar_t* type, const wchar_t* text, DWORD style, int id, int x, int y, int width, int height);
    void CreateControls();
    void RefreshPorts();
    void Connect();
    void Disconnect(bool manual = true);
    void OnMidi(DWORD raw);
    void OnAction(const mackie::Action& action);
    void OnFrame(std::unique_ptr<AudioFrame> frame);
    void Tick();
    void Render();
    void Command(int id, int notification);
    void FillCommands();
    void FillBindings();
    void ApplyWindowsPreset();
    void ExecuteCommand(const std::wstring& id);
    void CommandLoop();
    void Save();
    void Restart();
    void Quit();
    void TrayMenu();
    const AudioStripState* Find(const std::wstring& key) const;
    std::wstring RowKey() const;
    void Status(const std::wstring& text);

    HWND window_ = nullptr;
    HWND encoderWindow_ = nullptr;
    HWND jogWindow_ = nullptr;
    HFONT font_ = nullptr, titleFont_ = nullptr;
    HBRUSH background_ = nullptr;
    HINSTANCE instance_ = nullptr;
    NOTIFYICONDATAW tray_{};
    std::unique_ptr<NativeAudioController> audio_;
    std::unique_ptr<AudioFrame> frame_;
    std::vector<MidiPortName> inputs_, outputs_;
    std::vector<std::wstring> rowKeys_;
    std::uint64_t started_ = 0, renderDue_ = 0, deviceScanDue_ = 0;
    bool smoke_ = false, closing_ = false, diagnostics_ = false;
    bool renderGuard_ = false;
    std::wstring status_;
    std::mutex commandMutex_;
    std::condition_variable commandWake_;
    std::deque<std::function<bool()>> commandQueue_;
    bool commandRunning_ = true;
    std::thread commandWorker_;
};
