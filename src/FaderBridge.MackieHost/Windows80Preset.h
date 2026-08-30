#pragma once
#include <array>

// Project-owned Windows command preset. MIDI channel 16, Note 0..79.
// This is device-independent; it does not program a surface or open a MIDI port.
// Keep order synchronized with docs/mackie-touchscreen-example.csv (build checked).
inline constexpr std::array<const wchar_t*, 80> Windows80Commands = {{
    L"OpenTaskManager", // 0: 任务管理器
    L"OpenTerminal", // 1: 终端
    L"OpenRun", // 2: 运行
    L"OpenSettings", // 3: 设置
    L"OpenFileExplorer", // 4: 资源管理器
    L"OpenHomeFolder", // 5: 个人文件夹
    L"OpenDownloadsFolder", // 6: 下载
    L"OpenDocumentsFolder", // 7: 文档
    L"OpenCalculator", // 8: 计算器
    L"OpenNotepad", // 9: 记事本
    L"OpenSnippingTool", // 10: 截图工具
    L"OpenQuickSettings", // 11: 快速设置
    L"OpenSearch", // 12: 搜索
    L"OpenStart", // 13: 开始菜单
    L"OpenBluetoothSettings", // 14: 蓝牙
    L"OpenSystemAbout", // 15: 系统信息
    L"ShowDesktop", // 16: 显示桌面
    L"MinimizeForegroundWindow", // 17: 最小化
    L"MaximizeRestoreForegroundWindow", // 18: 最大化还原
    L"ToggleForegroundAlwaysOnTop", // 19: 置顶
    L"SnapWindowLeft", // 20: 靠左
    L"SnapWindowRight", // 21: 靠右
    L"SnapWindowTopHalf", // 22: 上半屏
    L"SnapWindowBottomHalf", // 23: 下半屏
    L"MoveWindowNextMonitor", // 24: 下一显示器
    L"MoveWindowPreviousMonitor", // 25: 上一显示器
    L"SwitchNextWindow", // 26: 下一窗口
    L"SwitchPreviousWindow", // 27: 上一窗口
    L"OpenTaskView", // 28: 任务视图
    L"NewVirtualDesktop", // 29: 新桌面
    L"NextVirtualDesktop", // 30: 下一桌面
    L"PreviousVirtualDesktop", // 31: 上一桌面
    L"OpenThisPC", // 32: 此电脑
    L"OpenQuickAccess", // 33: 快速访问
    L"OpenDesktopFolder", // 34: 桌面文件夹
    L"OpenPicturesFolder", // 35: 图片
    L"ExplorerLargeIcons", // 36: 大图标
    L"ExplorerDetails", // 37: 详细信息
    L"ExplorerList", // 38: 列表
    L"ExplorerContent", // 39: 内容
    L"ExplorerPreviousFolder", // 40: 后退
    L"ExplorerNextFolder", // 41: 前进
    L"ExplorerParentFolder", // 42: 上一级
    L"ExplorerSearch", // 43: 搜索文件
    L"ExplorerNewFolder", // 44: 新建文件夹
    L"ExplorerProperties", // 45: 属性
    L"ExplorerTogglePreviewPane", // 46: 预览窗格
    L"ExplorerFitColumns", // 47: 适应列宽
    L"MediaPlayPause", // 48: 播放暂停
    L"MediaStop", // 49: 停止
    L"MediaPrevious", // 50: 上一首
    L"MediaNext", // 51: 下一首
    L"SystemVolumeMute", // 52: 系统静音
    L"SystemVolumeDown", // 53: 音量降低
    L"SystemVolumeUp", // 54: 音量提高
    L"MonoAudio", // 55: 单声道
    L"ClearSolo", // 56: 清除独奏
    L"OpenVolumeMixer", // 57: 音量合成器
    L"OpenSoundSettings", // 58: 声音设置
    L"OpenSoundOutputPanel", // 59: 选择输出
    L"OpenMusicFolder", // 60: 音乐文件夹
    L"OpenVideosFolder", // 61: 视频文件夹
    L"FullScreen", // 62: 全屏
    L"OpenCast", // 63: 投送
    L"Undo", // 64: 撤销
    L"Redo", // 65: 重做
    L"Cut", // 66: 剪切
    L"Copy", // 67: 复制
    L"Paste", // 68: 粘贴
    L"SelectAll", // 69: 全选
    L"Save", // 70: 保存
    L"SaveAs", // 71: 另存为
    L"Find", // 72: 查找
    L"Escape", // 73: 取消
    L"Enter", // 74: 确认
    L"NewTab", // 75: 新标签
    L"NextTab", // 76: 下个标签
    L"PreviousTab", // 77: 上个标签
    L"ReopenClosedTab", // 78: 恢复标签
    L"ClipboardHistory", // 79: 剪贴板历史
}};
