#include "WindowsCommandExecutor.h"

#include "DiagnosticLog.h"
#include "../BridgeGlobalShortcut.h"

#include <Windows.h>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

namespace
{
bool Launch(const std::wstring& target, const wchar_t* parameters = nullptr)
{
    const auto result = ShellExecuteW(nullptr, L"open", target.c_str(), parameters,
        nullptr, SW_SHOWNORMAL);
    const auto success = reinterpret_cast<INT_PTR>(result) > 32;
    if (!success)
    {
        FB_TRACE("WINDOWS_COMMAND_LAUNCH_FAILED target=%ls error=%lld", target.c_str(),
            static_cast<long long>(reinterpret_cast<INT_PTR>(result)));
    }
    return success;
}

std::wstring SystemPath(const wchar_t* relativePath)
{
    std::wstring directory(32768U, L'\0');
    const auto length = GetSystemDirectoryW(directory.data(),
        static_cast<UINT>(directory.size()));
    if (length == 0 || length >= directory.size()) return relativePath;
    directory.resize(length);
    return (std::filesystem::path(directory) / relativePath).wstring();
}

std::wstring WindowsPath(const wchar_t* relativePath)
{
    std::wstring directory(32768U, L'\0');
    const auto length = GetWindowsDirectoryW(directory.data(),
        static_cast<UINT>(directory.size()));
    if (length == 0 || length >= directory.size()) return relativePath;
    directory.resize(length);
    return (std::filesystem::path(directory) / relativePath).wstring();
}

bool LaunchSystem(const wchar_t* relativePath, const wchar_t* parameters = nullptr)
{
    return Launch(SystemPath(relativePath), parameters);
}

bool LaunchExplorer(const wchar_t* parameters = nullptr)
{
    return Launch(WindowsPath(L"explorer.exe"), parameters);
}

bool OpenKnownFolder(const KNOWNFOLDERID& folderId)
{
    PWSTR path = nullptr;
    const auto result = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &path);
    const auto success = SUCCEEDED(result) && path && Launch(path);
    if (FAILED(result))
    {
        FB_TRACE("WINDOWS_COMMAND_KNOWN_FOLDER_FAILED error=0x%08X",
            static_cast<unsigned>(result));
    }
    CoTaskMemFree(path);
    return success;
}

bool IsExtendedKey(const WORD key)
{
    switch (key)
    {
    case VK_LWIN:
    case VK_RWIN:
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_UP:
    case VK_RIGHT:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_SNAPSHOT:
        return true;
    default:
        return false;
    }
}

INPUT KeyInput(const WORD key, const bool keyUp)
{
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.dwFlags = (keyUp ? KEYEVENTF_KEYUP : 0U) |
        (IsExtendedKey(key) ? KEYEVENTF_EXTENDEDKEY : 0U);
    return input;
}

bool SendShortcut(const std::initializer_list<WORD> modifiers, const WORD key)
{
    std::vector<INPUT> inputs;
    inputs.reserve(modifiers.size() * 2U + 2U);
    for (const auto modifier : modifiers) inputs.push_back(KeyInput(modifier, false));
    inputs.push_back(KeyInput(key, false));
    inputs.push_back(KeyInput(key, true));
    for (auto modifier = modifiers.end(); modifier != modifiers.begin();)
    {
        --modifier;
        inputs.push_back(KeyInput(*modifier, true));
    }
    const auto sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent == inputs.size()) return true;
    FB_TRACE("WINDOWS_COMMAND_SEND_INPUT_FAILED sent=%u expected=%u error=%lu", sent,
        static_cast<unsigned>(inputs.size()), GetLastError());
    return false;
}

bool TapKey(const WORD key)
{
    return SendShortcut({}, key);
}

HWND ForegroundWindow()
{
    const auto window = GetForegroundWindow();
    return window && IsWindow(window) ? window : nullptr;
}

bool ForegroundProcessIs(const wchar_t* expectedExecutable)
{
    const auto window = ForegroundWindow();
    if (!window) return false;

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0) return false;

    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return false;

    std::wstring path(32768U, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    const auto queried = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!queried || length == 0) return false;

    path.resize(length);
    const auto executable = std::filesystem::path(path).filename().wstring();
    return _wcsicmp(executable.c_str(), expectedExecutable) == 0;
}

bool SetExplorerView(const WORD numberKey)
{
    // Microsoft's documented File Explorer view commands are foreground
    // shortcuts. Refuse to inject them into an unrelated application.
    if (!ForegroundProcessIs(L"explorer.exe"))
    {
        FB_TRACE("WINDOWS_COMMAND_EXPLORER_NOT_FOREGROUND");
        return false;
    }
    return SendShortcut({ VK_CONTROL, VK_SHIFT }, numberKey);
}

bool MinimizeForegroundWindow()
{
    const auto window = ForegroundWindow();
    return window && ShowWindowAsync(window, SW_MINIMIZE) != FALSE;
}

bool MaximizeRestoreForegroundWindow()
{
    const auto window = ForegroundWindow();
    if (!window) return false;
    return ShowWindowAsync(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE) != FALSE;
}

bool CloseForegroundWindow()
{
    const auto window = ForegroundWindow();
    return window && PostMessageW(window, WM_CLOSE, 0, 0) != FALSE;
}

bool ToggleForegroundAlwaysOnTop()
{
    const auto window = ForegroundWindow();
    if (!window) return false;
    const auto topmost = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    return SetWindowPos(window, topmost ? HWND_NOTOPMOST : HWND_TOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
}

bool OpenTempFolder()
{
    std::wstring path(32768U, L'\0');
    const auto length = GetTempPathW(static_cast<DWORD>(path.size()), path.data());
    if (length == 0 || length >= path.size()) return false;
    path.resize(length);
    return Launch(path);
}
}

bool WindowsCommandExecutor::Execute(const WindowsCommand command)
{
    bool result = false;
    switch (command)
    {
    case WindowsCommand::OpenTaskManager: result = LaunchSystem(L"taskmgr.exe"); break;
    case WindowsCommand::OpenTerminal:
        result = Launch(L"wt.exe");
        if (!result) result = LaunchSystem(L"WindowsPowerShell\\v1.0\\powershell.exe");
        break;
    case WindowsCommand::OpenPowerShell:
        result = LaunchSystem(L"WindowsPowerShell\\v1.0\\powershell.exe"); break;
    case WindowsCommand::OpenCommandPrompt: result = LaunchSystem(L"cmd.exe"); break;
    case WindowsCommand::OpenRun: result = SendShortcut({ VK_LWIN }, 'R'); break;
    case WindowsCommand::OpenFileExplorer: result = LaunchExplorer(); break;
    case WindowsCommand::OpenControlPanel: result = LaunchSystem(L"control.exe"); break;
    case WindowsCommand::OpenSystemInformation: result = LaunchSystem(L"msinfo32.exe"); break;
    case WindowsCommand::OpenComputerManagement: result = LaunchSystem(L"compmgmt.msc"); break;
    case WindowsCommand::OpenDeviceManager: result = LaunchSystem(L"devmgmt.msc"); break;
    case WindowsCommand::OpenDiskManagement: result = LaunchSystem(L"diskmgmt.msc"); break;
    case WindowsCommand::OpenServices: result = LaunchSystem(L"services.msc"); break;
    case WindowsCommand::OpenEventViewer: result = LaunchSystem(L"eventvwr.msc"); break;
    case WindowsCommand::OpenRegistryEditor: result = LaunchSystem(L"regedit.exe"); break;
    case WindowsCommand::OpenResourceMonitor: result = LaunchSystem(L"resmon.exe"); break;
    case WindowsCommand::OpenCalculator: result = LaunchSystem(L"calc.exe"); break;
    case WindowsCommand::OpenNotepad: result = LaunchSystem(L"notepad.exe"); break;
    case WindowsCommand::OpenPaint: result = LaunchSystem(L"mspaint.exe"); break;
    case WindowsCommand::OpenCharacterMap: result = LaunchSystem(L"charmap.exe"); break;
    case WindowsCommand::OpenSnippingTool: result = LaunchSystem(L"SnippingTool.exe"); break;
    case WindowsCommand::OpenPowerUserMenu: result = SendShortcut({ VK_LWIN }, 'X'); break;
    case WindowsCommand::OpenQuickAssist: result = SendShortcut({ VK_LWIN, VK_CONTROL }, 'Q'); break;
    case WindowsCommand::OpenSoundOutputPanel: result = SendShortcut({ VK_LWIN, VK_CONTROL }, 'V'); break;
    case WindowsCommand::ToggleCalendarAndClock: result = SendShortcut({ VK_LWIN, VK_MENU }, 'D'); break;
    case WindowsCommand::OpenSystemAbout: result = Launch(L"ms-settings:about"); break;

    case WindowsCommand::OpenSettings: result = Launch(L"ms-settings:"); break;
    case WindowsCommand::OpenWindowsUpdate: result = Launch(L"ms-settings:windowsupdate"); break;
    case WindowsCommand::OpenSoundSettings: result = Launch(L"ms-settings:sound"); break;
    case WindowsCommand::OpenVolumeMixer: result = Launch(L"ms-settings:apps-volume"); break;
    case WindowsCommand::OpenDisplaySettings: result = Launch(L"ms-settings:display"); break;
    case WindowsCommand::OpenNetworkSettings: result = Launch(L"ms-settings:network-status"); break;
    case WindowsCommand::OpenBluetoothSettings: result = Launch(L"ms-settings:bluetooth"); break;
    case WindowsCommand::OpenInstalledApps: result = Launch(L"ms-settings:appsfeatures"); break;
    case WindowsCommand::OpenStartupApps: result = Launch(L"ms-settings:startupapps"); break;
    case WindowsCommand::OpenDefaultApps: result = Launch(L"ms-settings:defaultapps"); break;
    case WindowsCommand::OpenStorageSettings: result = Launch(L"ms-settings:storagesense"); break;
    case WindowsCommand::OpenPowerSettings: result = Launch(L"ms-settings:powersleep"); break;
    case WindowsCommand::OpenDateTimeSettings: result = Launch(L"ms-settings:dateandtime"); break;
    case WindowsCommand::OpenClipboardSettings: result = Launch(L"ms-settings:clipboard"); break;
    case WindowsCommand::OpenMicrophonePrivacy: result = Launch(L"ms-settings:privacy-microphone"); break;
    case WindowsCommand::OpenCameraPrivacy: result = Launch(L"ms-settings:privacy-webcam"); break;
    case WindowsCommand::OpenAccessibilitySettings: result = Launch(L"ms-settings:easeofaccess"); break;
    case WindowsCommand::OpenPrinterSettings: result = Launch(L"ms-settings:printers"); break;
    case WindowsCommand::OpenWindowsSecurity: result = Launch(L"windowsdefender:"); break;

    case WindowsCommand::OpenHomeFolder: result = OpenKnownFolder(FOLDERID_Profile); break;
    case WindowsCommand::OpenDesktopFolder: result = OpenKnownFolder(FOLDERID_Desktop); break;
    case WindowsCommand::OpenDocumentsFolder: result = OpenKnownFolder(FOLDERID_Documents); break;
    case WindowsCommand::OpenDownloadsFolder: result = OpenKnownFolder(FOLDERID_Downloads); break;
    case WindowsCommand::OpenMusicFolder: result = OpenKnownFolder(FOLDERID_Music); break;
    case WindowsCommand::OpenPicturesFolder: result = OpenKnownFolder(FOLDERID_Pictures); break;
    case WindowsCommand::OpenVideosFolder: result = OpenKnownFolder(FOLDERID_Videos); break;
    case WindowsCommand::OpenThisPC: result = LaunchExplorer(L"shell:MyComputerFolder"); break;
    case WindowsCommand::OpenQuickAccess: result = LaunchExplorer(); break;
    case WindowsCommand::OpenNetworkFolder: result = LaunchExplorer(L"shell:NetworkPlacesFolder"); break;
    case WindowsCommand::OpenRecycleBin: result = LaunchExplorer(L"shell:RecycleBinFolder"); break;
    case WindowsCommand::OpenRoamingAppData: result = OpenKnownFolder(FOLDERID_RoamingAppData); break;
    case WindowsCommand::OpenLocalAppData: result = OpenKnownFolder(FOLDERID_LocalAppData); break;
    case WindowsCommand::OpenTempFolder: result = OpenTempFolder(); break;
    case WindowsCommand::OpenStartupFolder: result = OpenKnownFolder(FOLDERID_Startup); break;

    case WindowsCommand::ExplorerExtraLargeIcons: result = SetExplorerView('1'); break;
    case WindowsCommand::ExplorerLargeIcons: result = SetExplorerView('2'); break;
    case WindowsCommand::ExplorerMediumIcons: result = SetExplorerView('3'); break;
    case WindowsCommand::ExplorerSmallIcons: result = SetExplorerView('4'); break;
    case WindowsCommand::ExplorerList: result = SetExplorerView('5'); break;
    case WindowsCommand::ExplorerDetails: result = SetExplorerView('6'); break;
    case WindowsCommand::ExplorerTiles: result = SetExplorerView('7'); break;
    case WindowsCommand::ExplorerContent: result = SetExplorerView('8'); break;
    case WindowsCommand::ExplorerNewFolder:
        result = ForegroundProcessIs(L"explorer.exe") && SendShortcut({ VK_CONTROL, VK_SHIFT }, 'N'); break;
    case WindowsCommand::ExplorerProperties:
        result = ForegroundProcessIs(L"explorer.exe") && SendShortcut({ VK_MENU }, VK_RETURN); break;
    case WindowsCommand::ExplorerTogglePreviewPane:
        result = ForegroundProcessIs(L"explorer.exe") && SendShortcut({ VK_MENU }, 'P'); break;
    case WindowsCommand::ExplorerToggleDetailsPane:
        result = ForegroundProcessIs(L"explorer.exe") && SendShortcut({ VK_MENU, VK_SHIFT }, 'P'); break;
    case WindowsCommand::ExplorerPreviousFolder:
        result = ForegroundProcessIs(L"explorer.exe") && SendShortcut({ VK_MENU }, VK_LEFT); break;
    case WindowsCommand::ExplorerNextFolder:
        result = ForegroundProcessIs(L"explorer.exe") && SendShortcut({ VK_MENU }, VK_RIGHT); break;
    case WindowsCommand::ExplorerParentFolder:
        result = ForegroundProcessIs(L"explorer.exe") && SendShortcut({ VK_MENU }, VK_UP); break;
    case WindowsCommand::ExplorerSearch:
        result = ForegroundProcessIs(L"explorer.exe") && TapKey(VK_F3); break;
    case WindowsCommand::ExplorerFitColumns:
        result = ForegroundProcessIs(L"explorer.exe") && SendShortcut({ VK_CONTROL }, VK_ADD); break;

    case WindowsCommand::OpenStart: result = TapKey(VK_LWIN); break;
    case WindowsCommand::OpenSearch: result = SendShortcut({ VK_LWIN }, 'S'); break;
    case WindowsCommand::OpenQuickSettings: result = SendShortcut({ VK_LWIN }, 'A'); break;
    case WindowsCommand::OpenNotifications: result = SendShortcut({ VK_LWIN }, 'N'); break;
    case WindowsCommand::ShowDesktop: result = SendShortcut({ VK_LWIN }, 'D'); break;
    case WindowsCommand::MinimizeAll: result = SendShortcut({ VK_LWIN }, 'M'); break;
    case WindowsCommand::RestoreMinimized: result = SendShortcut({ VK_LWIN, VK_SHIFT }, 'M'); break;
    case WindowsCommand::OpenTaskView: result = SendShortcut({ VK_LWIN }, VK_TAB); break;
    case WindowsCommand::SwitchNextWindow: result = SendShortcut({ VK_MENU }, VK_TAB); break;
    case WindowsCommand::SwitchPreviousWindow: result = SendShortcut({ VK_MENU, VK_SHIFT }, VK_TAB); break;
    case WindowsCommand::MinimizeForegroundWindow: result = MinimizeForegroundWindow(); break;
    case WindowsCommand::MaximizeRestoreForegroundWindow: result = MaximizeRestoreForegroundWindow(); break;
    case WindowsCommand::CloseForegroundWindow: result = CloseForegroundWindow(); break;
    case WindowsCommand::ToggleForegroundAlwaysOnTop: result = ToggleForegroundAlwaysOnTop(); break;
    case WindowsCommand::SnapWindowLeft: result = SendShortcut({ VK_LWIN }, VK_LEFT); break;
    case WindowsCommand::SnapWindowRight: result = SendShortcut({ VK_LWIN }, VK_RIGHT); break;
    case WindowsCommand::SnapWindowUp: result = SendShortcut({ VK_LWIN }, VK_UP); break;
    case WindowsCommand::SnapWindowDown: result = SendShortcut({ VK_LWIN }, VK_DOWN); break;
    case WindowsCommand::MoveWindowNextMonitor: result = SendShortcut({ VK_LWIN, VK_SHIFT }, VK_RIGHT); break;
    case WindowsCommand::MoveWindowPreviousMonitor: result = SendShortcut({ VK_LWIN, VK_SHIFT }, VK_LEFT); break;
    case WindowsCommand::OpenProjectDisplay: result = SendShortcut({ VK_LWIN }, 'P'); break;
    case WindowsCommand::OpenCast: result = SendShortcut({ VK_LWIN }, 'K'); break;
    case WindowsCommand::LockComputer: result = LockWorkStation() != FALSE; break;
    case WindowsCommand::OpenSnapLayouts: result = SendShortcut({ VK_LWIN }, 'Z'); break;
    case WindowsCommand::SnapWindowTopHalf: result = SendShortcut({ VK_LWIN, VK_MENU }, VK_UP); break;
    case WindowsCommand::SnapWindowBottomHalf: result = SendShortcut({ VK_LWIN, VK_MENU }, VK_DOWN); break;
    case WindowsCommand::ToggleOtherWindowsMinimized: result = SendShortcut({ VK_LWIN }, VK_HOME); break;
    case WindowsCommand::PeekDesktop: result = SendShortcut({ VK_LWIN }, VK_OEM_COMMA); break;
    case WindowsCommand::OpenWindowMenu: result = SendShortcut({ VK_MENU }, VK_SPACE); break;

    case WindowsCommand::NewVirtualDesktop: result = SendShortcut({ VK_LWIN, VK_CONTROL }, 'D'); break;
    case WindowsCommand::CloseVirtualDesktop: result = SendShortcut({ VK_LWIN, VK_CONTROL }, VK_F4); break;
    case WindowsCommand::NextVirtualDesktop: result = SendShortcut({ VK_LWIN, VK_CONTROL }, VK_RIGHT); break;
    case WindowsCommand::PreviousVirtualDesktop: result = SendShortcut({ VK_LWIN, VK_CONTROL }, VK_LEFT); break;

    case WindowsCommand::OpenPinnedApp1: result = SendShortcut({ VK_LWIN }, '1'); break;
    case WindowsCommand::OpenPinnedApp2: result = SendShortcut({ VK_LWIN }, '2'); break;
    case WindowsCommand::OpenPinnedApp3: result = SendShortcut({ VK_LWIN }, '3'); break;
    case WindowsCommand::OpenPinnedApp4: result = SendShortcut({ VK_LWIN }, '4'); break;
    case WindowsCommand::OpenPinnedApp5: result = SendShortcut({ VK_LWIN }, '5'); break;
    case WindowsCommand::OpenPinnedApp6: result = SendShortcut({ VK_LWIN }, '6'); break;
    case WindowsCommand::OpenPinnedApp7: result = SendShortcut({ VK_LWIN }, '7'); break;
    case WindowsCommand::OpenPinnedApp8: result = SendShortcut({ VK_LWIN }, '8'); break;
    case WindowsCommand::OpenPinnedApp9: result = SendShortcut({ VK_LWIN }, '9'); break;
    case WindowsCommand::OpenPinnedApp10: result = SendShortcut({ VK_LWIN }, '0'); break;
    case WindowsCommand::NextTaskbarApp: result = SendShortcut({ VK_LWIN }, 'T'); break;
    case WindowsCommand::PreviousTaskbarApp: result = SendShortcut({ VK_LWIN, VK_SHIFT }, 'T'); break;
    case WindowsCommand::FocusNotificationArea: result = SendShortcut({ VK_LWIN }, 'B'); break;

    case WindowsCommand::Undo: result = SendShortcut({ VK_CONTROL }, 'Z'); break;
    case WindowsCommand::Redo: result = SendShortcut({ VK_CONTROL }, 'Y'); break;
    case WindowsCommand::Cut: result = SendShortcut({ VK_CONTROL }, 'X'); break;
    case WindowsCommand::Copy: result = SendShortcut({ VK_CONTROL }, 'C'); break;
    case WindowsCommand::Paste: result = SendShortcut({ VK_CONTROL }, 'V'); break;
    case WindowsCommand::SelectAll: result = SendShortcut({ VK_CONTROL }, 'A'); break;
    case WindowsCommand::Save: result = SendShortcut({ VK_CONTROL }, 'S'); break;
    case WindowsCommand::SaveAs: result = SendShortcut({ VK_CONTROL, VK_SHIFT }, 'S'); break;
    case WindowsCommand::OpenFile: result = SendShortcut({ VK_CONTROL }, 'O'); break;
    case WindowsCommand::Find: result = SendShortcut({ VK_CONTROL }, 'F'); break;
    case WindowsCommand::Print: result = SendShortcut({ VK_CONTROL }, 'P'); break;
    case WindowsCommand::Rename: result = TapKey(VK_F2); break;
    case WindowsCommand::Escape: result = TapKey(VK_ESCAPE); break;
    case WindowsCommand::Enter: result = TapKey(VK_RETURN); break;
    case WindowsCommand::Delete: result = TapKey(VK_DELETE); break;
    case WindowsCommand::Backspace: result = TapKey(VK_BACK); break;
    case WindowsCommand::NextField: result = TapKey(VK_TAB); break;
    case WindowsCommand::PreviousField: result = SendShortcut({ VK_SHIFT }, VK_TAB); break;
    case WindowsCommand::ContextMenu: result = SendShortcut({ VK_SHIFT }, VK_F10); break;

    case WindowsCommand::NewTab: result = SendShortcut({ VK_CONTROL }, 'T'); break;
    case WindowsCommand::CloseTab: result = SendShortcut({ VK_CONTROL }, 'W'); break;
    case WindowsCommand::ReopenClosedTab: result = SendShortcut({ VK_CONTROL, VK_SHIFT }, 'T'); break;
    case WindowsCommand::NextTab: result = SendShortcut({ VK_CONTROL }, VK_TAB); break;
    case WindowsCommand::PreviousTab: result = SendShortcut({ VK_CONTROL, VK_SHIFT }, VK_TAB); break;
    case WindowsCommand::FocusAddressBar: result = SendShortcut({ VK_CONTROL }, 'L'); break;
    case WindowsCommand::BrowserBack: result = SendShortcut({ VK_MENU }, VK_LEFT); break;
    case WindowsCommand::BrowserForward: result = SendShortcut({ VK_MENU }, VK_RIGHT); break;
    case WindowsCommand::BrowserRefresh: result = SendShortcut({ VK_CONTROL }, 'R'); break;
    case WindowsCommand::NewWindow: result = SendShortcut({ VK_CONTROL }, 'N'); break;
    case WindowsCommand::FullScreen: result = TapKey(VK_F11); break;
    case WindowsCommand::ZoomIn: result = SendShortcut({ VK_CONTROL }, VK_OEM_PLUS); break;
    case WindowsCommand::ZoomOut: result = SendShortcut({ VK_CONTROL }, VK_OEM_MINUS); break;
    case WindowsCommand::ZoomReset: result = SendShortcut({ VK_CONTROL }, '0'); break;
    case WindowsCommand::PageTop: result = TapKey(VK_HOME); break;
    case WindowsCommand::PageBottom: result = TapKey(VK_END); break;
    case WindowsCommand::PageUp: result = TapKey(VK_PRIOR); break;
    case WindowsCommand::PageDown: result = TapKey(VK_NEXT); break;

    case WindowsCommand::MediaPlayPause: result = TapKey(VK_MEDIA_PLAY_PAUSE); break;
    case WindowsCommand::MediaNext: result = TapKey(VK_MEDIA_NEXT_TRACK); break;
    case WindowsCommand::MediaPrevious: result = TapKey(VK_MEDIA_PREV_TRACK); break;
    case WindowsCommand::MediaStop: result = TapKey(VK_MEDIA_STOP); break;
    case WindowsCommand::SystemVolumeMute: result = TapKey(VK_VOLUME_MUTE); break;
    case WindowsCommand::SystemVolumeUp: result = TapKey(VK_VOLUME_UP); break;
    case WindowsCommand::SystemVolumeDown: result = TapKey(VK_VOLUME_DOWN); break;

    case WindowsCommand::CaptureRegion: result = SendShortcut({ VK_LWIN, VK_SHIFT }, 'S'); break;
    case WindowsCommand::CaptureFullScreen: result = SendShortcut({ VK_LWIN }, VK_SNAPSHOT); break;
    case WindowsCommand::CaptureActiveWindow: result = SendShortcut({ VK_MENU }, VK_SNAPSHOT); break;
    case WindowsCommand::ToggleGameBar: result = SendShortcut({ VK_LWIN }, 'G'); break;
    case WindowsCommand::ToggleScreenRecording: result = SendShortcut({ VK_LWIN, VK_MENU }, 'R'); break;
    case WindowsCommand::VoiceTyping: result = SendShortcut({ VK_LWIN }, 'H'); break;
    case WindowsCommand::EmojiPanel: result = SendShortcut({ VK_LWIN }, VK_OEM_PERIOD); break;
    case WindowsCommand::ClipboardHistory: result = SendShortcut({ VK_LWIN }, 'V'); break;

    case WindowsCommand::NextInputLanguage: result = SendShortcut({ VK_LWIN }, VK_SPACE); break;
    case WindowsCommand::PreviousInputLanguage: result = SendShortcut({ VK_LWIN, VK_SHIFT }, VK_SPACE); break;
    case WindowsCommand::PreviousInputMethod: result = SendShortcut({ VK_LWIN, VK_CONTROL }, VK_SPACE); break;

    case WindowsCommand::OpenOnScreenKeyboard: result = LaunchSystem(L"osk.exe"); break;
    case WindowsCommand::OpenMagnifier: result = LaunchSystem(L"Magnify.exe"); break;
    case WindowsCommand::CloseMagnifier: result = SendShortcut({ VK_LWIN }, VK_ESCAPE); break;
    case WindowsCommand::MagnifierZoomIn: result = SendShortcut({ VK_LWIN }, VK_OEM_PLUS); break;
    case WindowsCommand::MagnifierZoomOut: result = SendShortcut({ VK_LWIN }, VK_OEM_MINUS); break;
    case WindowsCommand::ToggleNarrator: result = SendShortcut({ VK_LWIN, VK_CONTROL }, VK_RETURN); break;
    case WindowsCommand::ToggleColorFilters: result = SendShortcut({ VK_LWIN, VK_CONTROL }, 'C'); break;

    case WindowsCommand::FocusWindowsEucon:
        result = bridge::RequestSummon(bridge::Application::WindowsEucon); break;
    case WindowsCommand::FocusUadEucon:
        result = bridge::RequestSummon(bridge::Application::UadEucon); break;
    case WindowsCommand::FocusMackieControl:
        result = bridge::RequestSummon(bridge::Application::MackieControl); break;
    }
    FB_TRACE("WINDOWS_COMMAND_EXECUTE command=%u result=%d",
        static_cast<unsigned>(command), result ? 1 : 0);
    return result;
}
