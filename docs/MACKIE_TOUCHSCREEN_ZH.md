# Windows 80 键触屏预设 — 中文说明

这是桥接程序的通用 MIDI 命令预设，不是新的 Mackie 协议，也不是设备驱动。
P1-Nano 由 iMAP 管理屏幕；其他设备可发送同样的 MIDI Note，使用同一组命令。

## 本次设备安排

- **DAW 2：Windows 功能**，以原版 Cubase 为基础，只替换 5 页共 80 个触屏键。
- **DAW 3：保留原版 Cubase**。DAW 1 也不会被生成器更改。
- 第二组 USB MIDI 输入、输出对应 DAW 2；不要选择 iMAP 使用的第四组端口。
- MIDI **通道 16** 是自定义命令的消息通道，与 USB 第几组端口无关。
- 推子、旋钮、触摸、分页、Mute/Solo/Rec 和实体 Transport 不由此预设更改。

## 安装一次，以后直接使用

1. 在 iMAP 导出并保留自己的原始完整配置。
2. 底部 **LOAD FILE** 加载生成的 `Windows80-DAW2.imap`，再选择顶部第二个 DAW 槽位。
3. 在桥接程序断开 MIDI，点击 **一键应用 Windows 80 键预设**。旧配置会另存备份；已有冲突分配不会被覆盖。
4. 输入选 `MIDIIN2 (iCON P1-Nano)`，输出选 `MIDIOUT2 (iCON P1-Nano)`，选择 P1-Nano 配置并保留触摸保护，连接设备。
5. 验证屏幕和按键后，在 iMAP 点 **SAVE AS USER DEFAULT**，让当前设置下次继续使用。

名称可能随驱动变化，以实际带有第二组编号的配对端口为准。只有用户明确选择的配对才能打开。

两种文件并不相同：

- `.imap`：底部 Save File / Load File，保存完整配置，包括三个槽位和各 DAW 映射。
- `.p1n-daw`：右键 Save / Load DAW mapping，只导入当前选中的单个 DAW。用它之前务必先选择正确槽位。

完整文件只适合从它自己的原始备份更新；如果你之后更改过其他槽位，优先用单 DAW 文件，避免把新修改还原为旧快照。
仓库不分发厂商预设，生成器要求用户自行提供 iMAP 导出文件；生成物和原始备份都不进入 Git。

## 五页按键位置

每页按照从左到右、从上到下编号。屏幕使用短英文，下面对应中文用途；括号里是屏幕标签。

### 第 1 页：系统与工具（Note 0–15）

| 第 1 列 | 第 2 列 | 第 3 列 | 第 4 列 |
| --- | --- | --- | --- |
| 任务管理器<br>Task Mgr | 终端<br>Terminal | 运行<br>Run | 设置<br>Settings |
| 资源管理器<br>Explorer | 个人文件夹<br>Home | 下载<br>Downloads | 文档<br>Documents |
| 计算器<br>Calc | 记事本<br>Notepad | 截图工具<br>Snip | 快速设置<br>Quick Set |
| 搜索<br>Search | 开始菜单<br>Start | 蓝牙<br>Bluetooth | 系统信息<br>Sys Info |

### 第 2 页：窗口与桌面（Note 16–31）

| 第 1 列 | 第 2 列 | 第 3 列 | 第 4 列 |
| --- | --- | --- | --- |
| 显示桌面<br>Desktop | 最小化<br>Minimize | 最大化还原<br>Max Restore | 置顶<br>Pin Window |
| 靠左<br>Snap Left | 靠右<br>Snap Right | 上半屏<br>Top Half | 下半屏<br>Bottom Half |
| 下一显示器<br>Next Screen | 上一显示器<br>Prev Screen | 下一窗口<br>Next Window | 上一窗口<br>Prev Window |
| 任务视图<br>Task View | 新桌面<br>New Desk | 下一桌面<br>Next Desk | 上一桌面<br>Prev Desk |

### 第 3 页：文件管理（Note 32–47）

| 第 1 列 | 第 2 列 | 第 3 列 | 第 4 列 |
| --- | --- | --- | --- |
| 此电脑<br>This PC | 快速访问<br>Quick Files | 桌面文件夹<br>Desk Folder | 图片<br>Pictures |
| 大图标<br>Big Icons | 详细信息<br>Details | 列表<br>List | 内容<br>Content |
| 后退<br>Back | 前进<br>Forward | 上一级<br>Up Folder | 搜索文件<br>Search |
| 新建文件夹<br>New Folder | 属性<br>Properties | 预览窗格<br>Preview | 适应列宽<br>Fit Columns |

### 第 4 页：媒体与音频（Note 48–63）

| 第 1 列 | 第 2 列 | 第 3 列 | 第 4 列 |
| --- | --- | --- | --- |
| 播放暂停<br>Play Pause | 停止<br>Stop | 上一首<br>Prev Track | 下一首<br>Next Track |
| 系统静音<br>Mute | 音量降低<br>Vol Down | 音量提高<br>Vol Up | 单声道<br>Mono |
| 清除独奏<br>Clear Solo | 音量合成器<br>Vol Mixer | 声音设置<br>Sound | 选择输出<br>Output |
| 音乐文件夹<br>Music | 视频文件夹<br>Videos | 全屏<br>Full Screen | 投送<br>Cast |

### 第 5 页：编辑与快捷操作（Note 64–79）

| 第 1 列 | 第 2 列 | 第 3 列 | 第 4 列 |
| --- | --- | --- | --- |
| 撤销<br>Undo | 重做<br>Redo | 剪切<br>Cut | 复制<br>Copy |
| 粘贴<br>Paste | 全选<br>Select All | 保存<br>Save | 另存为<br>Save As |
| 查找<br>Find | 取消<br>Cancel | 确认<br>Enter | 新标签<br>New Tab |
| 下个标签<br>Next Tab | 上个标签<br>Prev Tab | 恢复标签<br>Reopen Tab | 剪贴板历史<br>Clipboard |

## 使用边界

- 窗口、编辑、文件和浏览器快捷操作作用于当前前台窗口；对应软件不支持快捷键时，不能保证有效。
- 触屏媒体命令使用 Windows 的系统媒体控制；系统决定接收它的播放器。实体 Transport 保留桥接程序已有的所选应用媒体逻辑。
- Mono 切换 Windows 单声道；Clear Solo 清除本桥接程序管理的 Solo 并恢复之前的静音状态，不会强行取消用户原本手动设置的所有静音。
- 触屏静态页面颜色、标签由 iMAP 管理；不能把 MIDI 发送成功当成设备灯光已验证。
- 在 iMAP 选择 Note 类型：按下应发送 Note On，松开发 Note Off 或力度为 0 的 Note On。需要实机确认释放报文，否则重复按键可能失效。
- 更细的命令说明见 [Windows 命令中文手册](WINDOWS_COMMANDS_ZH.md)。其中 EuControl 分配步骤不适用于 Mackie 版。

## 重新生成（面向开发者）

```powershell
.\scripts\make-mackie-touchscreen-preset.ps1 -InputPreset '自己导出的.imap' -DawSlot 2
```

输出在 `artifacts/mackie-presets/`。脚本拒绝覆盖已有文件、拒绝不认识的布局，不修改正在运行的 iMAP 配置，也不打开 MIDI 端口。
当前核实的编辑器版本是 iMAP 1.25.3、导出格式标记 1.32。这是经过编辑器核对的配置转换，不是厂商公开承诺的文件格式 API；其他版本须重新验证。
