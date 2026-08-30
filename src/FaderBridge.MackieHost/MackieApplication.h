#pragma once
#include "WinMidiPort.h"
#include "MackieSurface.h"
#include "MackieSettings.h"
#include "MackieMedia.h"
#include "NativeAudioController.h"
#include "WindowsCommand.h"
#include <CommCtrl.h>
#include <Shellapi.h>
#include <condition_variable>
#include <deque>

class MackieApplication final
{
public:
    explicit MackieApplication(bool smoke);
    ~MackieApplication();
    int Run(HINSTANCE instance, int show);
private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Message(UINT, WPARAM, LPARAM);
    HWND Control(const wchar_t* type, const wchar_t* text, DWORD style, int id, int x, int y, int width, int height);
    void CreateControls();
    void RefreshPorts();
    void Connect();
    void Disconnect();
    void OnMidi(DWORD raw);
    void OnAction(const mackie::Action& action);
    void OnFrame(std::unique_ptr<AudioFrame> frame);
    void Tick();
    void Render();
    void Command(int id, int notification);
    void FillCommands();
    void FillBindings();
    void FillRoutes();
    void ExecuteCommand(const std::wstring& id);
    void CommandLoop();
    void Save();
    void Quit();
    void TrayMenu();
    const AudioStripState* Find(const std::wstring& key) const;
    std::wstring RowKey() const;
    void Status(const std::wstring& text);

    HWND window_ = nullptr;
    HFONT font_ = nullptr, titleFont_ = nullptr;
    HBRUSH background_ = nullptr;
    HINSTANCE instance_ = nullptr;
    NOTIFYICONDATAW tray_{};
    MackieSettings settings_;
    WinMidiPort midi_;
    mackie::Surface surface_;
    std::unique_ptr<NativeAudioController> audio_;
    std::unique_ptr<MackieMedia> media_;
    std::unique_ptr<AudioFrame> frame_;
    std::vector<MidiPortName> inputs_, outputs_;
    std::vector<std::wstring> rowKeys_;
    std::wstring routeKey_;
    std::wstring observedOutputRoute_, observedInputRoute_;
    std::vector<AudioRouteOption> outputRoutes_, inputRoutes_;
    std::array<bool, 2048> customPressed_{};
    std::map<int, bool> customFeedback_;
    std::uint64_t started_ = 0, renderDue_ = 0, saveDue_ = 0;
    bool smoke_ = false, closing_ = false, learning_ = false, dirtySettings_ = false;
    bool renderGuard_ = false;
    std::wstring learningCommand_, status_, mediaStatus_;
    UINT selectedInput_ = 0, selectedOutput_ = 0;
    std::mutex commandMutex_;
    std::condition_variable commandWake_;
    std::deque<std::function<bool()>> commandQueue_;
    bool commandRunning_ = true;
    std::thread commandWorker_;
};
