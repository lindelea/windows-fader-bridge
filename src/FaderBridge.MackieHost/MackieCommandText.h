#pragma once
#include "MackieEncoderCommands.h"
#include "MackieJogInput.h"
#include <cwctype>
#include <sstream>

namespace mackie::ui
{
// Project-owned translations from our Chinese command manual, not vendor data.
struct Translation { const wchar_t* id; const wchar_t* name; const wchar_t* description; };
inline constexpr Translation Translations[] = {
    {L"MonoAudio", L"单声道音频", L"切换 Windows 主输出的单声道/立体声状态，LED 显示真实状态。"} ,
    {L"ClearSolo", L"清除独奏", L"取消所有应用通道的 Solo，并恢复进入 Solo 前的静音状态。"} ,
    {L"OpenTaskManager", L"任务管理器", L"查看程序、CPU、内存、磁盘和启动项。"} ,
    {L"OpenTerminal", L"Windows 终端", L"打开 Windows Terminal；不可用时回退到 PowerShell。"} ,
    {L"OpenPowerShell", L"PowerShell", L"打开 PowerShell 命令窗口。"} ,
    {L"OpenCommandPrompt", L"命令提示符", L"打开传统 CMD 命令窗口。"} ,
    {L"OpenRun", L"运行", L"打开 Windows“运行”对话框。"} ,
    {L"OpenFileExplorer", L"文件资源管理器", L"打开新的资源管理器窗口。"} ,
    {L"OpenControlPanel", L"控制面板", L"打开传统控制面板。"} ,
    {L"OpenSystemInformation", L"系统信息", L"查看硬件、驱动和系统详细资料。"} ,
    {L"OpenComputerManagement", L"计算机管理", L"打开磁盘、事件、用户等综合管理工具。"} ,
    {L"OpenDeviceManager", L"设备管理器", L"查看和管理硬件设备及驱动。"} ,
    {L"OpenDiskManagement", L"磁盘管理", L"查看分区、磁盘和卷。"} ,
    {L"OpenServices", L"服务", L"查看 Windows 后台服务。"} ,
    {L"OpenEventViewer", L"事件查看器", L"查看系统和应用事件日志。"} ,
    {L"OpenRegistryEditor", L"注册表编辑器", L"打开注册表编辑器；修改前应谨慎。"} ,
    {L"OpenResourceMonitor", L"资源监视器", L"查看更详细的 CPU、内存、磁盘和网络占用。"} ,
    {L"OpenCalculator", L"计算器", L"打开 Windows 计算器。"} ,
    {L"OpenNotepad", L"记事本", L"打开 Windows 记事本。"} ,
    {L"OpenPaint", L"画图", L"打开 Windows 画图。"} ,
    {L"OpenCharacterMap", L"字符映射表", L"查找并复制特殊字符。"} ,
    {L"OpenSnippingTool", L"截图工具", L"打开 Windows 截图工具。"} ,
    {L"OpenPowerUserMenu", L"高级用户菜单", L"打开 Win+X 系统快捷菜单。"} ,
    {L"OpenQuickAssist", L"快速助手", L"打开 Windows 远程协助工具。"} ,
    {L"OpenSoundOutputPanel", L"声音输出面板", L"打开快速设置中的输出设备、空间音频和音量混合器入口。"} ,
    {L"ToggleCalendarAndClock", L"日期和时钟", L"显示或隐藏任务栏日期时间面板。"} ,
    {L"OpenSystemAbout", L"系统“关于”", L"打开设置中的系统“关于”页面。"} ,
    {L"OpenSettings", L"设置主页", L"打开 Windows 设置。"} ,
    {L"OpenWindowsUpdate", L"Windows 更新", L"直接打开系统更新页面。"} ,
    {L"OpenSoundSettings", L"声音设置", L"打开输入、输出和音量设置。"} ,
    {L"OpenVolumeMixer", L"音量混合器", L"打开每个应用的音量和设备路由页面。"} ,
    {L"OpenDisplaySettings", L"显示设置", L"调整分辨率、缩放、HDR 和多显示器。"} ,
    {L"OpenNetworkSettings", L"网络设置", L"查看网络状态和网络配置。"} ,
    {L"OpenBluetoothSettings", L"蓝牙", L"打开蓝牙和设备页面。"} ,
    {L"OpenInstalledApps", L"已安装的应用", L"查看、修改或卸载应用。"} ,
    {L"OpenStartupApps", L"启动应用", L"管理开机自动启动的软件。"} ,
    {L"OpenDefaultApps", L"默认应用", L"设置浏览器、播放器等默认程序。"} ,
    {L"OpenStorageSettings", L"存储设置", L"查看磁盘占用和存储管理。"} ,
    {L"OpenPowerSettings", L"电源设置", L"调整电源、睡眠和节能选项。"} ,
    {L"OpenDateTimeSettings", L"日期和时间", L"调整时间、时区和自动校时。"} ,
    {L"OpenClipboardSettings", L"剪贴板设置", L"管理剪贴板历史和同步。"} ,
    {L"OpenMicrophonePrivacy", L"麦克风隐私", L"管理哪些应用可以使用麦克风。"} ,
    {L"OpenCameraPrivacy", L"摄像头隐私", L"管理哪些应用可以使用摄像头。"} ,
    {L"OpenAccessibilitySettings", L"辅助功能", L"打开 Windows 辅助功能设置。"} ,
    {L"OpenPrinterSettings", L"打印机", L"管理打印机和扫描仪。"} ,
    {L"OpenWindowsSecurity", L"Windows 安全中心", L"打开病毒防护、防火墙等安全页面。"} ,
    {L"OpenHomeFolder", L"个人文件夹", L"打开当前用户的主目录。"} ,
    {L"OpenDesktopFolder", L"桌面文件夹", L"打开桌面文件所在目录。"} ,
    {L"OpenDocumentsFolder", L"文档", L"打开当前用户的文档文件夹。"} ,
    {L"OpenDownloadsFolder", L"下载", L"打开下载文件夹。"} ,
    {L"OpenMusicFolder", L"音乐", L"打开音乐文件夹。"} ,
    {L"OpenPicturesFolder", L"图片", L"打开图片文件夹。"} ,
    {L"OpenVideosFolder", L"视频", L"打开视频文件夹。"} ,
    {L"OpenThisPC", L"此电脑", L"查看磁盘、设备和常用文件夹。"} ,
    {L"OpenQuickAccess", L"快速访问", L"打开资源管理器主页/快速访问。"} ,
    {L"OpenNetworkFolder", L"网络", L"查看局域网设备和共享。"} ,
    {L"OpenRecycleBin", L"回收站", L"打开回收站。"} ,
    {L"OpenRoamingAppData", L"漫游应用数据", L"打开当前用户的 Roaming AppData。"} ,
    {L"OpenLocalAppData", L"本地应用数据", L"打开当前用户的 Local AppData。"} ,
    {L"OpenTempFolder", L"临时文件夹", L"打开当前用户的临时目录。"} ,
    {L"OpenStartupFolder", L"启动文件夹", L"打开当前用户的开机启动快捷方式目录。"} ,
    {L"ExplorerExtraLargeIcons", L"超大图标", L"使用最大的文件图标显示。"} ,
    {L"ExplorerLargeIcons", L"大图标", L"使用大图标显示，适合图片和封面。"} ,
    {L"ExplorerMediumIcons", L"中等图标", L"使用中等大小图标。"} ,
    {L"ExplorerSmallIcons", L"小图标", L"使用较小图标，显示更多项目。"} ,
    {L"ExplorerList", L"列表", L"以紧凑列表方式显示。"} ,
    {L"ExplorerDetails", L"详细信息", L"显示名称、日期、类型、大小等列。"} ,
    {L"ExplorerTiles", L"平铺", L"以平铺方式显示文件及简要信息。"} ,
    {L"ExplorerContent", L"内容", L"每个文件占一行并显示更多信息。"} ,
    {L"ExplorerNewFolder", L"新建文件夹", L"在当前目录新建文件夹。"} ,
    {L"ExplorerProperties", L"属性", L"打开当前选中文件或文件夹的属性。"} ,
    {L"ExplorerTogglePreviewPane", L"切换预览窗格", L"显示或隐藏右侧文件预览。"} ,
    {L"ExplorerToggleDetailsPane", L"切换详细信息窗格", L"显示或隐藏所选文件的详细资料。"} ,
    {L"ExplorerPreviousFolder", L"上一个文件夹", L"回到浏览历史中的上一位置。"} ,
    {L"ExplorerNextFolder", L"下一个文件夹", L"前进到浏览历史中的下一位置。"} ,
    {L"ExplorerParentFolder", L"上级文件夹", L"返回当前路径的上一级。"} ,
    {L"ExplorerSearch", L"搜索当前文件夹", L"将焦点放到文件搜索。"} ,
    {L"ExplorerFitColumns", L"自动调整列宽", L"在详细信息视图中让各列适合内容宽度。"} ,
    {L"OpenStart", L"开始菜单", L"打开或关闭开始菜单。"} ,
    {L"OpenSearch", L"Windows 搜索", L"打开系统搜索。"} ,
    {L"OpenQuickSettings", L"快速设置", L"打开网络、声音、电源等快速设置。"} ,
    {L"OpenNotifications", L"通知中心", L"打开通知和日历区域。"} ,
    {L"ShowDesktop", L"显示桌面", L"显示桌面；再次执行可返回原窗口。"} ,
    {L"MinimizeAll", L"最小化全部", L"最小化所有普通窗口。"} ,
    {L"RestoreMinimized", L"恢复最小化窗口", L"恢复刚刚由“最小化全部”隐藏的窗口。"} ,
    {L"OpenTaskView", L"任务视图", L"显示全部窗口和虚拟桌面。"} ,
    {L"SwitchNextWindow", L"下一个窗口", L"按打开窗口顺序切换到下一个窗口。"} ,
    {L"SwitchPreviousWindow", L"上一个窗口", L"反向切换窗口。"} ,
    {L"MinimizeForegroundWindow", L"最小化当前窗口", L"最小化最前面的窗口。"} ,
    {L"MaximizeRestoreForegroundWindow", L"最大化/还原", L"在最大化与普通窗口大小之间切换。"} ,
    {L"CloseForegroundWindow", L"关闭当前窗口", L"正常请求关闭当前窗口；软件仍可提示保存。"} ,
    {L"ToggleForegroundAlwaysOnTop", L"切换窗口置顶", L"让当前窗口保持最前，或取消置顶。"} ,
    {L"SnapWindowLeft", L"贴靠左侧", L"把当前窗口贴到屏幕左侧。"} ,
    {L"SnapWindowRight", L"贴靠右侧", L"把当前窗口贴到屏幕右侧。"} ,
    {L"SnapWindowUp", L"向上贴靠/最大化", L"按 Windows 默认规则向上调整窗口。"} ,
    {L"SnapWindowDown", L"向下贴靠/还原", L"按 Windows 默认规则向下调整窗口。"} ,
    {L"MoveWindowNextMonitor", L"移到下一个显示器", L"把当前窗口移到右侧显示器。"} ,
    {L"MoveWindowPreviousMonitor", L"移到上一个显示器", L"把当前窗口移到左侧显示器。"} ,
    {L"OpenProjectDisplay", L"投影模式", L"选择仅电脑、复制、扩展或第二屏幕。"} ,
    {L"OpenCast", L"无线投屏", L"打开连接无线显示器的面板。"} ,
    {L"LockComputer", L"锁定电脑", L"立即进入 Windows 锁屏。"} ,
    {L"OpenSnapLayouts", L"窗口布局", L"打开 Windows 11 的贴靠布局选择。"} ,
    {L"SnapWindowTopHalf", L"贴靠上半屏", L"把当前窗口放到屏幕上半部分。"} ,
    {L"SnapWindowBottomHalf", L"贴靠下半屏", L"把当前窗口放到屏幕下半部分。"} ,
    {L"ToggleOtherWindowsMinimized", L"仅保留当前窗口", L"最小化/恢复除当前窗口外的其他窗口。"} ,
    {L"PeekDesktop", L"临时查看桌面", L"按住命令期间临时透视桌面。"} ,
    {L"OpenWindowMenu", L"窗口系统菜单", L"打开当前窗口的移动、大小、最小化和关闭菜单。"} ,
    {L"NewVirtualDesktop", L"新建虚拟桌面", L"创建并切换到新的虚拟桌面。"} ,
    {L"CloseVirtualDesktop", L"关闭虚拟桌面", L"关闭当前虚拟桌面，窗口会移到其他桌面。"} ,
    {L"NextVirtualDesktop", L"下一个虚拟桌面", L"切换到右侧虚拟桌面。"} ,
    {L"PreviousVirtualDesktop", L"上一个虚拟桌面", L"切换到左侧虚拟桌面。"} ,
    {L"OpenPinnedApp1", L"固定应用 1", L"启动/切换任务栏从左数第 1 个固定应用。"} ,
    {L"OpenPinnedApp2", L"固定应用 2", L"启动/切换任务栏从左数第 2 个固定应用。"} ,
    {L"OpenPinnedApp3", L"固定应用 3", L"启动/切换任务栏从左数第 3 个固定应用。"} ,
    {L"OpenPinnedApp4", L"固定应用 4", L"启动/切换任务栏从左数第 4 个固定应用。"} ,
    {L"OpenPinnedApp5", L"固定应用 5", L"启动/切换任务栏从左数第 5 个固定应用。"} ,
    {L"OpenPinnedApp6", L"固定应用 6", L"启动/切换任务栏从左数第 6 个固定应用。"} ,
    {L"OpenPinnedApp7", L"固定应用 7", L"启动/切换任务栏从左数第 7 个固定应用。"} ,
    {L"OpenPinnedApp8", L"固定应用 8", L"启动/切换任务栏从左数第 8 个固定应用。"} ,
    {L"OpenPinnedApp9", L"固定应用 9", L"启动/切换任务栏从左数第 9 个固定应用。"} ,
    {L"OpenPinnedApp10", L"固定应用 10", L"启动/切换任务栏从左数第 10 个固定应用。"} ,
    {L"NextTaskbarApp", L"下一个任务栏应用", L"在任务栏图标之间向后移动焦点。"} ,
    {L"PreviousTaskbarApp", L"上一个任务栏应用", L"在任务栏图标之间向前移动焦点。"} ,
    {L"FocusNotificationArea", L"通知区域", L"将焦点移到任务栏右侧托盘图标。"} ,
    {L"Undo", L"撤销", L"撤销上一步操作。"} ,
    {L"Redo", L"重做", L"恢复刚刚撤销的操作。"} ,
    {L"Cut", L"剪切", L"剪切选中内容。"} ,
    {L"Copy", L"复制", L"复制选中内容。"} ,
    {L"Paste", L"粘贴", L"粘贴剪贴板内容。"} ,
    {L"SelectAll", L"全选", L"选择当前区域的全部内容。"} ,
    {L"Save", L"保存", L"保存当前文档或项目。"} ,
    {L"SaveAs", L"另存为", L"打开“另存为”窗口。"} ,
    {L"OpenFile", L"打开", L"打开软件的文件选择窗口。"} ,
    {L"Find", L"查找", L"在当前软件或页面中查找。"} ,
    {L"Print", L"打印", L"打开打印窗口。"} ,
    {L"Rename", L"重命名", L"重命名选中的文件或项目。"} ,
    {L"Escape", L"退出/取消", L"关闭菜单、取消选择或退出当前操作。"} ,
    {L"Enter", L"确认", L"等同于键盘回车。"} ,
    {L"Delete", L"删除", L"执行普通删除；具体行为由当前软件决定。"} ,
    {L"Backspace", L"退格", L"删除前一个字符或执行软件的返回操作。"} ,
    {L"NextField", L"下一个输入项", L"将焦点移到下一个控件。"} ,
    {L"PreviousField", L"上一个输入项", L"将焦点移到上一个控件。"} ,
    {L"ContextMenu", L"右键菜单", L"打开当前项目的上下文菜单。"} ,
    {L"NewTab", L"新建标签页", L"新建并切换到标签页。"} ,
    {L"CloseTab", L"关闭标签页", L"关闭当前标签；没有标签时软件可能关闭窗口。"} ,
    {L"ReopenClosedTab", L"恢复关闭的标签页", L"重新打开最近关闭的标签页。"} ,
    {L"NextTab", L"下一个标签页", L"切换到右侧/下一个标签。"} ,
    {L"PreviousTab", L"上一个标签页", L"切换到左侧/上一个标签。"} ,
    {L"FocusAddressBar", L"地址栏", L"将焦点放到网址或路径输入框。"} ,
    {L"BrowserBack", L"后退", L"返回上一页面或位置。"} ,
    {L"BrowserForward", L"前进", L"前进到下一页面或位置。"} ,
    {L"BrowserRefresh", L"刷新", L"重新加载页面或窗口。"} ,
    {L"NewWindow", L"新建窗口", L"新建当前软件的窗口。"} ,
    {L"FullScreen", L"全屏", L"切换当前软件的全屏显示。"} ,
    {L"ZoomIn", L"放大", L"放大网页或文档。"} ,
    {L"ZoomOut", L"缩小", L"缩小网页或文档。"} ,
    {L"ZoomReset", L"恢复缩放", L"将缩放恢复为 100%。"} ,
    {L"PageTop", L"页面顶部", L"跳到页面开头。"} ,
    {L"PageBottom", L"页面底部", L"跳到页面末尾。"} ,
    {L"PageUp", L"向上翻页", L"向上滚动一页。"} ,
    {L"PageDown", L"向下翻页", L"向下滚动一页。"} ,
    {L"MediaPlayPause", L"播放/暂停", L"控制当前由 Windows 接管的媒体会话。"} ,
    {L"MediaNext", L"下一曲", L"切换到下一首或下一个媒体项目。"} ,
    {L"MediaPrevious", L"上一曲", L"返回上一首或上一个媒体项目。"} ,
    {L"MediaStop", L"停止", L"停止当前媒体；部分播放器可能不支持。"} ,
    {L"SystemVolumeMute", L"系统静音", L"切换 Windows 主输出静音。"} ,
    {L"SystemVolumeUp", L"系统音量增加", L"增加 Windows 主输出音量。"} ,
    {L"SystemVolumeDown", L"系统音量降低", L"降低 Windows 主输出音量。"} ,
    {L"CaptureRegion", L"区域截图", L"打开区域截图选择界面。"} ,
    {L"CaptureFullScreen", L"保存全屏截图", L"截取整个屏幕并保存到“图片/屏幕截图”。"} ,
    {L"CaptureActiveWindow", L"复制当前窗口截图", L"截取最前面的窗口并复制到剪贴板。"} ,
    {L"ToggleGameBar", L"游戏栏", L"打开 Windows Game Bar。"} ,
    {L"ToggleScreenRecording", L"屏幕录制", L"开始或停止 Game Bar 录制；当前软件必须支持。"} ,
    {L"VoiceTyping", L"语音输入", L"打开 Windows 语音听写。"} ,
    {L"EmojiPanel", L"表情面板", L"打开表情符号、GIF 和特殊字符面板。"} ,
    {L"ClipboardHistory", L"剪贴板历史", L"打开最近复制过的内容列表。"} ,
    {L"NextInputLanguage", L"下一输入语言", L"向后切换输入语言或键盘布局。"} ,
    {L"PreviousInputLanguage", L"上一输入语言", L"反向切换输入语言或键盘布局。"} ,
    {L"PreviousInputMethod", L"上次使用的输入法", L"返回之前选中的输入法。"} ,
    {L"OpenOnScreenKeyboard", L"屏幕键盘", L"打开 Windows 屏幕键盘。"} ,
    {L"OpenMagnifier", L"打开放大镜", L"启动 Windows 放大镜。"} ,
    {L"CloseMagnifier", L"关闭放大镜", L"关闭 Windows 放大镜。"} ,
    {L"MagnifierZoomIn", L"放大镜放大", L"提高放大镜倍率。"} ,
    {L"MagnifierZoomOut", L"放大镜缩小", L"降低放大镜倍率。"} ,
    {L"ToggleNarrator", L"切换讲述人", L"打开或关闭 Windows 屏幕朗读。"} ,
    {L"ToggleColorFilters", L"切换颜色滤镜", L"打开或关闭已在辅助功能设置中启用的颜色滤镜。"} ,
};
inline const Translation* Translated(std::wstring_view id)
{ for (const auto& t : Translations) if (id == t.id) return &t; return nullptr; }
inline std::wstring Lower(std::wstring s)
{ for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c)); return s; }
inline std::wstring Category(std::wstring_view name, bool zh)
{
    if (!zh) return std::wstring(name);
    constexpr const wchar_t* pairs[][2] = {
        {L"Windows Audio",L"Windows 音频"}, {L"System Tools",L"系统工具"}, {L"Settings",L"Windows 设置"},
        {L"Folders",L"个人文件夹"}, {L"Window Management",L"窗口管理"}, {L"Virtual Desktops",L"虚拟桌面"},
        {L"Taskbar",L"任务栏"}, {L"File Explorer",L"文件资源管理器"}, {L"Editing",L"编辑"},
        {L"Browser and Tabs",L"浏览器与标签页"}, {L"Media",L"媒体播放"}, {L"Capture and Input",L"截图与输入"},
        {L"Input and Language",L"输入与语言"}, {L"Accessibility",L"辅助功能"},
        {L"Current channel",L"当前通道"}, {L"Scroll and arrows",L"滚动与方向键"}, {L"Playback position",L"播放进度"}
    };
    for (const auto& p : pairs) if (name == p[0]) return p[1];
    return std::wstring(name);
}
inline std::wstring Name(std::wstring_view id, bool zh)
{
    if (id.empty()) return zh ? L"未分配" : L"Unassigned";
    if (auto c = FindMackieCommand(id)) { const auto t = Translated(id); return zh && t ? t->name : c->label; }
    if (auto c = mackie::FindCursorCommand(id))
    {
        const wchar_t* en[] = {L"Scroll up",L"Scroll down",L"Scroll left",L"Scroll right",
            L"Arrow up",L"Arrow down",L"Arrow left",L"Arrow right"};
        return zh ? c->label : en[static_cast<int>(c->input)];
    }
    if (id == L"Jog.SeekBack") return zh ? L"播放进度后退" : L"Seek backward";
    if (id == L"Jog.SeekForward") return zh ? L"播放进度前进" : L"Seek forward";
    if (auto c = FindMackieChannelCommand(id))
    {
        const wchar_t* en[] = {L"Volume +1%",L"Volume -1%",L"Pan left",L"Pan right",L"Center pan",
            L"Toggle mute",L"Toggle solo",L"Set default device",L"Bring application forward",
            L"Play / pause",L"Stop",L"Previous track",L"Next track",L"Position -1%",L"Position +1%",L"Repeat mode"};
        return zh ? c->label : en[c - MackieChannelCommands];
    }
    return std::wstring(id);
}
inline std::wstring Group(std::wstring_view id)
{
    if (auto c = FindMackieCommand(id)) return c->category;
    if (FindMackieChannelCommand(id)) return L"Current channel";
    if (mackie::FindCursorCommand(id)) return L"Scroll and arrows";
    return L"Playback position";
}
inline std::wstring Description(std::wstring_view id, bool zh)
{
    if (id.empty()) return zh ? L"此操作不执行任何命令。" : L"No command is sent for this control.";
    if (auto t = Translated(id); zh && t) return t->description;
    const auto group = Group(id);
    if (group == L"Current channel") return zh ? L"作用于当前选择的通道；由目标应用支持的能力决定是否可用。" : L"Targets the selected channel. Availability depends on the application's capabilities.";
    if (group == L"Playback position") return zh ? L"按每格秒数调整当前播放器的进度；播放器必须允许跳转。" : L"Moves playback by the configured seconds per tick. The player must support seeking.";
    if (group == L"File Explorer") return zh ? L"仅在文件资源管理器位于前台时生效。" : L"Available only while File Explorer is the foreground window.";
    if (group == L"Windows Audio") return id == L"MonoAudio" ? L"Toggle Windows mono audio. The LED follows the actual system state." : L"Clear all app solos and restore their pre-solo mute state.";
    if (group == L"System Tools" || group == L"Settings" || group == L"Folders")
        return L"Open " + Name(id, false) + L". Windows may ask for permission for administrative tools.";
    if (group == L"Media") return L"Send a system media command. The active player's support determines the result.";
    return zh ? L"作用于前台窗口或 Windows；不同应用对快捷操作的支持可能不同。" :
        L"Acts on the foreground window or Windows. Shortcut support varies between applications.";
}
inline bool Matches(std::wstring_view id, const std::wstring& query)
{
    auto corpus = Lower(Name(id,true) + L" " + Name(id,false) + L" " + Category(Group(id),true) + L" " + Group(id) + L" " + std::wstring(id));
    std::wistringstream terms(Lower(query)); std::wstring term;
    while (terms >> term) if (corpus.find(term) == std::wstring::npos) return false;
    return true;
}
}
