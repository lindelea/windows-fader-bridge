#include "WindowsCommandProcessor.h"

#include "DiagnosticLog.h"
#include "EuPrimitiveControl.h"
#include "EuPrimitiveSwitch.h"

#include <iterator>
#include <utility>

namespace
{
bool TraceSdkResult(const tERR result, const wchar_t* operation,
    const wchar_t* objectName)
{
    if (result == kERR_OK) return true;
    FB_TRACE("EUCON_COMMAND_INIT_FAILED operation=%ls object=%ls error=%d",
        operation, objectName, static_cast<int>(result));
    return false;
}
}

WindowsCommandProcessor::WindowsCommandProcessor(CommandHandler monoToggleHandler,
    CommandHandler clearSoloHandler, WindowsCommandHandler windowsCommandHandler)
    : monoToggleHandler_(std::move(monoToggleHandler)),
      clearSoloHandler_(std::move(clearSoloHandler)),
      windowsCommandHandler_(std::move(windowsCommandHandler)),
      windowsAudioCommands_(this), monoAudio_(this), clearSolo_(this)
{
    SetAttribute(kATRIBID_ProcessorType, kProcType_Command);
    SetAttribute(kATRIBID_ContainsSoftKeys, 1);
    SetAttribute(kATRIBID_SimpleUserVisibleName, tEuString(L"Key Commands"));
    SetAttribute(kATRIBID_DoNotSort, 1);
    SetPersistenceID(tEuString(L"FaderBridge.WindowsAudio.Commands.v1"));

    windowsAudioCommands_.SetId(WindowsAudioContainerId);
    windowsAudioCommands_.SetAttribute(
        kATRIBID_SimpleUserVisibleName, tEuString(L"Windows Audio"));
    windowsAudioCommands_.SetAttribute(kATRIBID_DoNotSort, 1);
    windowsAudioCommands_.SetPersistenceID(
        tEuString(L"FaderBridge.WindowsAudio.Commands.Container.v1"));
    AddControl(windowsAudioCommands_);

    monoAudio_.SetAttribute(kATRIBID_SimpleUserVisibleName, tEuString(L"Mono Audio"));
    monoAudio_.SetPersistenceID(
        tEuString(L"FaderBridge.WindowsAudio.Commands.MonoAudio.v1"));
    EuPrimitiveControl* primitive = nullptr;
    if (monoAudio_.GetPrimitive(EuControlSwitch::kID_Switch, &primitive) == kERR_OK &&
        primitive)
    {
        primitive->Initialize(kTYP_Int, 1U);
        primitive->LoadValueTableInterpolated(0, 0);
        if (auto* switchPrimitive = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            switchPrimitive->SetSwitchMode(kSWITCH_OneShot);
        }
    }
    // Windows is authoritative. The command is one-shot and its LED reflects
    // the current Windows accessibility setting, including external changes.
    monoAudio_.SetLedOverride(true);
    windowsAudioCommands_.PushBack(&monoAudio_, monoAudioMemberId_);

    clearSolo_.SetAttribute(kATRIBID_SimpleUserVisibleName, tEuString(L"Clear Solo"));
    clearSolo_.SetPersistenceID(
        tEuString(L"FaderBridge.WindowsAudio.Commands.ClearSolo.v1"));
    primitive = nullptr;
    if (clearSolo_.GetPrimitive(EuControlSwitch::kID_Switch, &primitive) == kERR_OK &&
        primitive)
    {
        primitive->Initialize(kTYP_Int, 1U);
        primitive->LoadValueTableInterpolated(0, 0);
        if (auto* switchPrimitive = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            switchPrimitive->SetSwitchMode(kSWITCH_OneShot);
        }
    }
    // Section 12.5 requires Clear Solo to remain lit while any channel is
    // soloed. This assignable command mirrors the standard System control.
    clearSolo_.SetLedOverride(true);
    windowsAudioCommands_.PushBack(&clearSolo_, clearSoloMemberId_);

    static constexpr CommandDefinition systemTools[] =
    {
        { WindowsCommand::OpenTaskManager, L"Task Manager", L"TaskManager" },
        { WindowsCommand::OpenTerminal, L"Windows Terminal", L"Terminal" },
        { WindowsCommand::OpenPowerShell, L"PowerShell", L"PowerShell" },
        { WindowsCommand::OpenCommandPrompt, L"Command Prompt", L"CommandPrompt" },
        { WindowsCommand::OpenRun, L"Run", L"Run" },
        { WindowsCommand::OpenFileExplorer, L"File Explorer", L"FileExplorer" },
        { WindowsCommand::OpenControlPanel, L"Control Panel", L"ControlPanel" },
        { WindowsCommand::OpenSystemInformation, L"System Information", L"SystemInformation" },
        { WindowsCommand::OpenComputerManagement, L"Computer Management", L"ComputerManagement" },
        { WindowsCommand::OpenDeviceManager, L"Device Manager", L"DeviceManager" },
        { WindowsCommand::OpenDiskManagement, L"Disk Management", L"DiskManagement" },
        { WindowsCommand::OpenServices, L"Services", L"Services" },
        { WindowsCommand::OpenEventViewer, L"Event Viewer", L"EventViewer" },
        { WindowsCommand::OpenRegistryEditor, L"Registry Editor", L"RegistryEditor" },
        { WindowsCommand::OpenResourceMonitor, L"Resource Monitor", L"ResourceMonitor" },
        { WindowsCommand::OpenCalculator, L"Calculator", L"Calculator" },
        { WindowsCommand::OpenNotepad, L"Notepad", L"Notepad" },
        { WindowsCommand::OpenPaint, L"Paint", L"Paint" },
        { WindowsCommand::OpenCharacterMap, L"Character Map", L"CharacterMap" },
        { WindowsCommand::OpenSnippingTool, L"Snipping Tool", L"SnippingTool" },
        { WindowsCommand::OpenPowerUserMenu, L"Power User Menu", L"PowerUserMenu" },
        { WindowsCommand::OpenQuickAssist, L"Quick Assist", L"QuickAssist" },
        { WindowsCommand::OpenSoundOutputPanel, L"Sound Output Panel", L"SoundOutputPanel" },
        { WindowsCommand::ToggleCalendarAndClock, L"Calendar and Clock", L"CalendarClock" },
        { WindowsCommand::OpenSystemAbout, L"System About", L"SystemAbout" },
    };
    static constexpr CommandDefinition settings[] =
    {
        { WindowsCommand::OpenSettings, L"Settings", L"Settings" },
        { WindowsCommand::OpenWindowsUpdate, L"Windows Update", L"WindowsUpdate" },
        { WindowsCommand::OpenSoundSettings, L"Sound Settings", L"Sound" },
        { WindowsCommand::OpenVolumeMixer, L"Volume Mixer", L"VolumeMixer" },
        { WindowsCommand::OpenDisplaySettings, L"Display Settings", L"Display" },
        { WindowsCommand::OpenNetworkSettings, L"Network Settings", L"Network" },
        { WindowsCommand::OpenBluetoothSettings, L"Bluetooth", L"Bluetooth" },
        { WindowsCommand::OpenInstalledApps, L"Installed Apps", L"InstalledApps" },
        { WindowsCommand::OpenStartupApps, L"Startup Apps", L"StartupApps" },
        { WindowsCommand::OpenDefaultApps, L"Default Apps", L"DefaultApps" },
        { WindowsCommand::OpenStorageSettings, L"Storage Settings", L"Storage" },
        { WindowsCommand::OpenPowerSettings, L"Power Settings", L"Power" },
        { WindowsCommand::OpenDateTimeSettings, L"Date and Time", L"DateTime" },
        { WindowsCommand::OpenClipboardSettings, L"Clipboard Settings", L"Clipboard" },
        { WindowsCommand::OpenMicrophonePrivacy, L"Microphone Privacy", L"MicrophonePrivacy" },
        { WindowsCommand::OpenCameraPrivacy, L"Camera Privacy", L"CameraPrivacy" },
        { WindowsCommand::OpenAccessibilitySettings, L"Accessibility", L"Accessibility" },
        { WindowsCommand::OpenPrinterSettings, L"Printers", L"Printers" },
        { WindowsCommand::OpenWindowsSecurity, L"Windows Security", L"WindowsSecurity" },
    };
    static constexpr CommandDefinition folders[] =
    {
        { WindowsCommand::OpenHomeFolder, L"Home Folder", L"Home" },
        { WindowsCommand::OpenDesktopFolder, L"Desktop", L"Desktop" },
        { WindowsCommand::OpenDocumentsFolder, L"Documents", L"Documents" },
        { WindowsCommand::OpenDownloadsFolder, L"Downloads", L"Downloads" },
        { WindowsCommand::OpenMusicFolder, L"Music", L"Music" },
        { WindowsCommand::OpenPicturesFolder, L"Pictures", L"Pictures" },
        { WindowsCommand::OpenVideosFolder, L"Videos", L"Videos" },
        { WindowsCommand::OpenThisPC, L"This PC", L"ThisPC" },
        { WindowsCommand::OpenQuickAccess, L"Quick Access", L"QuickAccess" },
        { WindowsCommand::OpenNetworkFolder, L"Network", L"Network" },
        { WindowsCommand::OpenRecycleBin, L"Recycle Bin", L"RecycleBin" },
        { WindowsCommand::OpenRoamingAppData, L"Roaming AppData", L"RoamingAppData" },
        { WindowsCommand::OpenLocalAppData, L"Local AppData", L"LocalAppData" },
        { WindowsCommand::OpenTempFolder, L"Temp Folder", L"Temp" },
        { WindowsCommand::OpenStartupFolder, L"Startup Folder", L"Startup" },
    };
    static constexpr CommandDefinition fileExplorer[] =
    {
        { WindowsCommand::ExplorerExtraLargeIcons, L"Extra Large Icons", L"ExtraLargeIcons" },
        { WindowsCommand::ExplorerLargeIcons, L"Large Icons", L"LargeIcons" },
        { WindowsCommand::ExplorerMediumIcons, L"Medium Icons", L"MediumIcons" },
        { WindowsCommand::ExplorerSmallIcons, L"Small Icons", L"SmallIcons" },
        { WindowsCommand::ExplorerList, L"List", L"List" },
        { WindowsCommand::ExplorerDetails, L"Details", L"Details" },
        { WindowsCommand::ExplorerTiles, L"Tiles", L"Tiles" },
        { WindowsCommand::ExplorerContent, L"Content", L"Content" },
        { WindowsCommand::ExplorerNewFolder, L"New Folder", L"NewFolder" },
        { WindowsCommand::ExplorerProperties, L"Properties", L"Properties" },
        { WindowsCommand::ExplorerTogglePreviewPane, L"Toggle Preview Pane", L"PreviewPane" },
        { WindowsCommand::ExplorerToggleDetailsPane, L"Toggle Details Pane", L"DetailsPane" },
        { WindowsCommand::ExplorerPreviousFolder, L"Previous Folder", L"PreviousFolder" },
        { WindowsCommand::ExplorerNextFolder, L"Next Folder", L"NextFolder" },
        { WindowsCommand::ExplorerParentFolder, L"Parent Folder", L"ParentFolder" },
        { WindowsCommand::ExplorerSearch, L"Search Folder", L"Search" },
        { WindowsCommand::ExplorerFitColumns, L"Fit Columns to Content", L"FitColumns" },
    };
    static constexpr CommandDefinition windows[] =
    {
        { WindowsCommand::OpenStart, L"Start Menu", L"Start" },
        { WindowsCommand::OpenSearch, L"Search", L"Search" },
        { WindowsCommand::OpenQuickSettings, L"Quick Settings", L"QuickSettings" },
        { WindowsCommand::OpenNotifications, L"Notifications", L"Notifications" },
        { WindowsCommand::ShowDesktop, L"Show Desktop", L"ShowDesktop" },
        { WindowsCommand::MinimizeAll, L"Minimize All", L"MinimizeAll" },
        { WindowsCommand::RestoreMinimized, L"Restore Minimized", L"RestoreMinimized" },
        { WindowsCommand::OpenTaskView, L"Task View", L"TaskView" },
        { WindowsCommand::SwitchNextWindow, L"Next Window", L"NextWindow" },
        { WindowsCommand::SwitchPreviousWindow, L"Previous Window", L"PreviousWindow" },
        { WindowsCommand::MinimizeForegroundWindow, L"Minimize Window", L"MinimizeWindow" },
        { WindowsCommand::MaximizeRestoreForegroundWindow, L"Maximize or Restore", L"MaximizeRestore" },
        { WindowsCommand::CloseForegroundWindow, L"Close Window", L"CloseWindow" },
        { WindowsCommand::ToggleForegroundAlwaysOnTop, L"Toggle Always on Top", L"AlwaysOnTop" },
        { WindowsCommand::SnapWindowLeft, L"Snap Left", L"SnapLeft" },
        { WindowsCommand::SnapWindowRight, L"Snap Right", L"SnapRight" },
        { WindowsCommand::SnapWindowUp, L"Snap Up", L"SnapUp" },
        { WindowsCommand::SnapWindowDown, L"Snap Down", L"SnapDown" },
        { WindowsCommand::MoveWindowNextMonitor, L"Move to Next Monitor", L"NextMonitor" },
        { WindowsCommand::MoveWindowPreviousMonitor, L"Move to Previous Monitor", L"PreviousMonitor" },
        { WindowsCommand::OpenProjectDisplay, L"Project Display", L"ProjectDisplay" },
        { WindowsCommand::OpenCast, L"Cast", L"Cast" },
        { WindowsCommand::LockComputer, L"Lock Computer", L"LockComputer" },
        { WindowsCommand::OpenSnapLayouts, L"Snap Layouts", L"SnapLayouts" },
        { WindowsCommand::SnapWindowTopHalf, L"Snap Top Half", L"SnapTopHalf" },
        { WindowsCommand::SnapWindowBottomHalf, L"Snap Bottom Half", L"SnapBottomHalf" },
        { WindowsCommand::ToggleOtherWindowsMinimized, L"Isolate Current Window", L"IsolateWindow" },
        { WindowsCommand::PeekDesktop, L"Peek at Desktop", L"PeekDesktop" },
        { WindowsCommand::OpenWindowMenu, L"Window Menu", L"WindowMenu" },
    };
    static constexpr CommandDefinition desktops[] =
    {
        { WindowsCommand::NewVirtualDesktop, L"New Desktop", L"NewDesktop" },
        { WindowsCommand::CloseVirtualDesktop, L"Close Desktop", L"CloseDesktop" },
        { WindowsCommand::NextVirtualDesktop, L"Next Desktop", L"NextDesktop" },
        { WindowsCommand::PreviousVirtualDesktop, L"Previous Desktop", L"PreviousDesktop" },
    };
    static constexpr CommandDefinition taskbar[] =
    {
        { WindowsCommand::OpenPinnedApp1, L"Pinned App 1", L"PinnedApp1" },
        { WindowsCommand::OpenPinnedApp2, L"Pinned App 2", L"PinnedApp2" },
        { WindowsCommand::OpenPinnedApp3, L"Pinned App 3", L"PinnedApp3" },
        { WindowsCommand::OpenPinnedApp4, L"Pinned App 4", L"PinnedApp4" },
        { WindowsCommand::OpenPinnedApp5, L"Pinned App 5", L"PinnedApp5" },
        { WindowsCommand::OpenPinnedApp6, L"Pinned App 6", L"PinnedApp6" },
        { WindowsCommand::OpenPinnedApp7, L"Pinned App 7", L"PinnedApp7" },
        { WindowsCommand::OpenPinnedApp8, L"Pinned App 8", L"PinnedApp8" },
        { WindowsCommand::OpenPinnedApp9, L"Pinned App 9", L"PinnedApp9" },
        { WindowsCommand::OpenPinnedApp10, L"Pinned App 10", L"PinnedApp10" },
        { WindowsCommand::NextTaskbarApp, L"Next Taskbar App", L"NextApp" },
        { WindowsCommand::PreviousTaskbarApp, L"Previous Taskbar App", L"PreviousApp" },
        { WindowsCommand::FocusNotificationArea, L"Notification Area", L"NotificationArea" },
    };
    static constexpr CommandDefinition editing[] =
    {
        { WindowsCommand::Undo, L"Undo", L"Undo" },
        { WindowsCommand::Redo, L"Redo", L"Redo" },
        { WindowsCommand::Cut, L"Cut", L"Cut" },
        { WindowsCommand::Copy, L"Copy", L"Copy" },
        { WindowsCommand::Paste, L"Paste", L"Paste" },
        { WindowsCommand::SelectAll, L"Select All", L"SelectAll" },
        { WindowsCommand::Save, L"Save", L"Save" },
        { WindowsCommand::SaveAs, L"Save As", L"SaveAs" },
        { WindowsCommand::OpenFile, L"Open", L"Open" },
        { WindowsCommand::Find, L"Find", L"Find" },
        { WindowsCommand::Print, L"Print", L"Print" },
        { WindowsCommand::Rename, L"Rename", L"Rename" },
        { WindowsCommand::Escape, L"Escape", L"Escape" },
        { WindowsCommand::Enter, L"Enter", L"Enter" },
        { WindowsCommand::Delete, L"Delete", L"Delete" },
        { WindowsCommand::Backspace, L"Backspace", L"Backspace" },
        { WindowsCommand::NextField, L"Next Field", L"NextField" },
        { WindowsCommand::PreviousField, L"Previous Field", L"PreviousField" },
        { WindowsCommand::ContextMenu, L"Context Menu", L"ContextMenu" },
    };
    static constexpr CommandDefinition browser[] =
    {
        { WindowsCommand::NewTab, L"New Tab", L"NewTab" },
        { WindowsCommand::CloseTab, L"Close Tab", L"CloseTab" },
        { WindowsCommand::ReopenClosedTab, L"Reopen Closed Tab", L"ReopenTab" },
        { WindowsCommand::NextTab, L"Next Tab", L"NextTab" },
        { WindowsCommand::PreviousTab, L"Previous Tab", L"PreviousTab" },
        { WindowsCommand::FocusAddressBar, L"Address Bar", L"AddressBar" },
        { WindowsCommand::BrowserBack, L"Back", L"Back" },
        { WindowsCommand::BrowserForward, L"Forward", L"Forward" },
        { WindowsCommand::BrowserRefresh, L"Refresh", L"Refresh" },
        { WindowsCommand::NewWindow, L"New Window", L"NewWindow" },
        { WindowsCommand::FullScreen, L"Full Screen", L"FullScreen" },
        { WindowsCommand::ZoomIn, L"Zoom In", L"ZoomIn" },
        { WindowsCommand::ZoomOut, L"Zoom Out", L"ZoomOut" },
        { WindowsCommand::ZoomReset, L"Reset Zoom", L"ZoomReset" },
        { WindowsCommand::PageTop, L"Page Top", L"PageTop" },
        { WindowsCommand::PageBottom, L"Page Bottom", L"PageBottom" },
        { WindowsCommand::PageUp, L"Page Up", L"PageUp" },
        { WindowsCommand::PageDown, L"Page Down", L"PageDown" },
    };
    static constexpr CommandDefinition media[] =
    {
        { WindowsCommand::MediaPlayPause, L"Play or Pause", L"PlayPause" },
        { WindowsCommand::MediaNext, L"Next Track", L"NextTrack" },
        { WindowsCommand::MediaPrevious, L"Previous Track", L"PreviousTrack" },
        { WindowsCommand::MediaStop, L"Stop", L"Stop" },
        { WindowsCommand::SystemVolumeMute, L"System Mute", L"SystemMute" },
        { WindowsCommand::SystemVolumeUp, L"System Volume Up", L"SystemVolumeUp" },
        { WindowsCommand::SystemVolumeDown, L"System Volume Down", L"SystemVolumeDown" },
    };
    static constexpr CommandDefinition captureInput[] =
    {
        { WindowsCommand::CaptureRegion, L"Screen Snip", L"ScreenSnip" },
        { WindowsCommand::CaptureFullScreen, L"Save Full Screenshot", L"FullScreenshot" },
        { WindowsCommand::CaptureActiveWindow, L"Copy Active Window", L"ActiveWindow" },
        { WindowsCommand::ToggleGameBar, L"Game Bar", L"GameBar" },
        { WindowsCommand::ToggleScreenRecording, L"Screen Recording", L"ScreenRecording" },
        { WindowsCommand::VoiceTyping, L"Voice Typing", L"VoiceTyping" },
        { WindowsCommand::EmojiPanel, L"Emoji Panel", L"EmojiPanel" },
        { WindowsCommand::ClipboardHistory, L"Clipboard History", L"ClipboardHistory" },
    };
    static constexpr CommandDefinition inputLanguage[] =
    {
        { WindowsCommand::NextInputLanguage, L"Next Input Language", L"NextLanguage" },
        { WindowsCommand::PreviousInputLanguage, L"Previous Input Language", L"PreviousLanguage" },
        { WindowsCommand::PreviousInputMethod, L"Previous Input Method", L"PreviousMethod" },
    };
    static constexpr CommandDefinition accessibility[] =
    {
        { WindowsCommand::OpenOnScreenKeyboard, L"On-Screen Keyboard", L"OnScreenKeyboard" },
        { WindowsCommand::OpenMagnifier, L"Open Magnifier", L"OpenMagnifier" },
        { WindowsCommand::CloseMagnifier, L"Close Magnifier", L"CloseMagnifier" },
        { WindowsCommand::MagnifierZoomIn, L"Magnifier Zoom In", L"MagnifierZoomIn" },
        { WindowsCommand::MagnifierZoomOut, L"Magnifier Zoom Out", L"MagnifierZoomOut" },
        { WindowsCommand::ToggleNarrator, L"Toggle Narrator", L"ToggleNarrator" },
        { WindowsCommand::ToggleColorFilters, L"Toggle Color Filters", L"ColorFilters" },
    };
    static constexpr CommandDefinition euconApplications[] =
    {
        { WindowsCommand::FocusWindowsEucon, L"Windows EUCON", L"WindowsEucon" },
        { WindowsCommand::FocusUadEucon, L"UAD EUCON", L"UadEucon" },
        { WindowsCommand::FocusMackieControl, L"Mackie Control", L"MackieControl" },
    };

    AddCategory(2U, L"System Tools", L"SystemTools", systemTools, std::size(systemTools));
    AddCategory(3U, L"Settings", L"Settings", settings, std::size(settings));
    AddCategory(4U, L"Folders", L"Folders", folders, std::size(folders));
    AddCategory(12U, L"File Explorer", L"FileExplorer", fileExplorer,
        std::size(fileExplorer));
    AddCategory(5U, L"Window Management", L"WindowManagement", windows, std::size(windows));
    AddCategory(6U, L"Virtual Desktops", L"VirtualDesktops", desktops, std::size(desktops));
    AddCategory(13U, L"Taskbar", L"Taskbar", taskbar, std::size(taskbar));
    AddCategory(7U, L"Editing", L"Editing", editing, std::size(editing));
    AddCategory(8U, L"Browser and Tabs", L"BrowserTabs", browser, std::size(browser));
    AddCategory(9U, L"Media", L"Media", media, std::size(media));
    AddCategory(10U, L"Capture and Input", L"CaptureInput", captureInput,
        std::size(captureInput));
    AddCategory(14U, L"Input and Language", L"InputLanguage", inputLanguage,
        std::size(inputLanguage));
    AddCategory(11U, L"Accessibility", L"Accessibility", accessibility,
        std::size(accessibility));
    AddCategory(15U, L"EUCON Applications", L"EuconApplications", euconApplications,
        std::size(euconApplications));
}

void WindowsCommandProcessor::AddCategory(const NEuCon::uint32 controlId,
    const wchar_t* name, const wchar_t* persistenceToken,
    const CommandDefinition* commands, const std::size_t commandCount)
{
    CommandCategory category;
    category.controlId = controlId;
    category.container = std::make_unique<EuControlSwitchArray>(this);
    TraceSdkResult(category.container->SetId(controlId), L"SetId", name);
    TraceSdkResult(category.container->SetAttribute(kATRIBID_SimpleUserVisibleName,
        tEuString(name)), L"SetUserVisibleName", name);
    TraceSdkResult(category.container->SetAttribute(kATRIBID_DoNotSort, 1),
        L"SetDoNotSort", name);
    const auto containerPersistenceId = std::wstring(
        L"FaderBridge.Windows.Commands.Container.") + persistenceToken + L".v1";
    TraceSdkResult(category.container->SetPersistenceID(
        tEuString(containerPersistenceId)), L"SetPersistenceID", name);
    TraceSdkResult(AddControl(*category.container), L"AddControl", name);

    category.commands.reserve(commandCount);
    for (std::size_t index = 0; index < commandCount; ++index)
    {
        const auto& definition = commands[index];
        CommandEntry entry;
        entry.command = definition.command;
        entry.control = std::make_unique<EuControlSwitch>(this);
        TraceSdkResult(entry.control->SetAttribute(kATRIBID_SimpleUserVisibleName,
            tEuString(definition.name)), L"SetUserVisibleName", definition.name);
        const auto commandPersistenceId = std::wstring(L"FaderBridge.Windows.Commands.") +
            persistenceToken + L"." + definition.persistenceToken + L".v1";
        TraceSdkResult(entry.control->SetPersistenceID(tEuString(commandPersistenceId)),
            L"SetPersistenceID", definition.name);

        EuPrimitiveControl* primitive = nullptr;
        const auto primitiveResult = entry.control->GetPrimitive(
            EuControlSwitch::kID_Switch, &primitive);
        TraceSdkResult(primitiveResult, L"GetSwitchPrimitive", definition.name);
        if (primitiveResult == kERR_OK && primitive)
        {
            TraceSdkResult(primitive->Initialize(kTYP_Int, 1U),
                L"InitializeSwitch", definition.name);
            if (auto* switchPrimitive = dynamic_cast<EuPrimitiveSwitch*>(primitive))
            {
                TraceSdkResult(switchPrimitive->SetSwitchMode(kSWITCH_OneShot),
                    L"SetOneShot", definition.name);
            }
        }
        // Application switching is a radio-like status across the EUCON apps:
        // this command processor can authoritatively identify Windows EUCON as
        // itself. The target applications remain ordinary one-shot commands.
        const bool selfApplication = definition.command == WindowsCommand::FocusWindowsEucon;
        TraceSdkResult(entry.control->SetLedOverride(selfApplication),
            L"SetLedOverride", definition.name);
        if (selfApplication)
        {
            EuPrimitiveControl* led = nullptr;
            if (entry.control->GetPrimitive(EuControlSwitch::kID_Led, &led) == kERR_OK && led)
            {
                TraceSdkResult(led->SetCurrentIndex(kLEDStatus_On),
                    L"SetSelfApplicationLed", definition.name);
                TraceSdkResult(led->Refresh(), L"RefreshSelfApplicationLed", definition.name);
            }
        }
        TraceSdkResult(category.container->PushBack(entry.control.get(), entry.memberId),
            L"PushBack", definition.name);
        category.commands.push_back(std::move(entry));
    }
    commandCategories_.push_back(std::move(category));
}

WindowsCommandProcessor::~WindowsCommandProcessor()
{
    for (auto category = commandCategories_.rbegin();
        category != commandCategories_.rend(); ++category)
    {
        for (auto command = category->commands.rbegin();
            command != category->commands.rend(); ++command)
        {
            category->container->Remove(command->memberId);
        }
        RemoveControl(*category->container);
    }
    commandCategories_.clear();
    windowsAudioCommands_.Remove(clearSoloMemberId_);
    windowsAudioCommands_.Remove(monoAudioMemberId_);
    RemoveControl(windowsAudioCommands_);
}

void WindowsCommandProcessor::SetSoloActive(const bool active)
{
    if (soloActive_ == active)
    {
        return;
    }
    soloActive_ = active;
    EuPrimitiveControl* primitive = nullptr;
    if (clearSolo_.GetPrimitive(EuControlSwitch::kID_Led, &primitive) == kERR_OK && primitive)
    {
        primitive->SetCurrentIndex(static_cast<NEuCon::uint16>(
            active ? kLEDStatus_On : kLEDStatus_Off));
        primitive->Refresh();
    }
}

void WindowsCommandProcessor::SetMonoAudioEnabled(const bool enabled)
{
    if (monoAudioEnabled_ == enabled)
    {
        return;
    }
    monoAudioEnabled_ = enabled;
    EuPrimitiveControl* primitive = nullptr;
    if (monoAudio_.GetPrimitive(EuControlSwitch::kID_Led, &primitive) == kERR_OK &&
        primitive)
    {
        primitive->SetCurrentIndex(static_cast<NEuCon::uint16>(
            enabled ? kLEDStatus_On : kLEDStatus_Off));
        primitive->Refresh();
    }
    FB_TRACE("MONO_AUDIO_LED enabled=%d", enabled ? 1 : 0);
}

void WindowsCommandProcessor::OnPrimitiveCallback(const tEVT eventType,
    const NEuCon::uint32,
    const NEuCon::uint32 controlId,
    const NEuCon::uint32 arrayMemberControlId,
    const NEuCon::uint32,
    EuPrimitiveControl*,
    const NEuCon::uint16,
    void*)
{
    // EUCON owns this callback thread. Queue only; all Windows work and all
    // EUCON feedback writes happen on their existing owning threads.
    if (eventType != kEVT_PRIM_StateChange)
    {
        return;
    }
    if (controlId == WindowsAudioContainerId &&
        arrayMemberControlId == monoAudioMemberId_ && monoToggleHandler_)
    {
        FB_TRACE("MONO_AUDIO_SURFACE_TOGGLE");
        monoToggleHandler_();
        return;
    }
    if (controlId == WindowsAudioContainerId &&
        arrayMemberControlId == clearSoloMemberId_ && clearSoloHandler_)
    {
        FB_TRACE("CLEAR_SOLO_COMMAND");
        clearSoloHandler_();
        return;
    }
    for (const auto& category : commandCategories_)
    {
        if (category.controlId != controlId) continue;
        for (const auto& command : category.commands)
        {
            if (command.memberId != arrayMemberControlId) continue;
            FB_TRACE("WINDOWS_COMMAND_SURFACE command=%u container=%u",
                static_cast<unsigned>(command.command),
                static_cast<unsigned>(controlId));
            if (windowsCommandHandler_) windowsCommandHandler_(command.command);
            return;
        }
        return;
    }
}
