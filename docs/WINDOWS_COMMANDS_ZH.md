# Windows Fader Bridge 可分配命令中文手册

本手册对应 Windows Fader Bridge 当前发布的 15 个分类、189 个 EUCON
可分配命令。EuControl 中显示的是英文名称，因此每个表格都保留了
“界面英文名”，方便直接查找和分配。

## 一、怎样分配

在 EuControl 的 Soft Keys 设置中，依次进入：

```text
Key Commands（按键命令）
  -> 分类
     -> 命令
```

Windows Fader Bridge 只负责提供命令；具体放到 S3、S4、S1、Dock、
Avid Control 或其他兼容表面的哪个按键，由 EuControl 管理。

## 二、使用前须知

- `Mono Audio` 和 `Clear Solo` 的 LED 表示真实 Windows 状态。
- 其他命令是一次性按键：按下时短暂亮灯，松开后熄灭。
- “窗口管理、编辑、浏览器”类命令通常作用于当前最前面的窗口。
- `File Explorer` 分类只在资源管理器位于最前面时执行，避免误操作其他软件。
- 不同软件对通用快捷键的支持可能不同；例如网页播放器未必支持全部媒体操作。
- 本目录没有关机、重启、永久删除、强制结束进程或绕过 Windows 安全确认的命令。

## 三、推荐的 S3 按键分配

### View 区

| 表面按键 | 推荐命令 | 用途 |
| --- | --- | --- |
| View1 | `Large Icons` | 资源管理器切换为大图标 |
| View2 | `Details` | 资源管理器切换为详细信息 |
| 上箭头 | `Parent Folder` | 返回上一级文件夹 |
| 左箭头 | `Previous Folder` | 返回刚才浏览的文件夹 |
| 右箭头 | `Next Folder` | 前进到下一个浏览记录 |
| 下箭头 | `Toggle Preview Pane` | 显示或隐藏文件预览 |

### 数字区

可以把数字 1–10 分配为 `Pinned App 1–10`。它们按照 Windows
任务栏从左到右的固定应用顺序工作：应用没打开就启动，已经打开就切换过去。

### A–F 快捷区示例

| 表面按键 | 推荐命令 | 用途 |
| --- | --- | --- |
| A | `Task Manager` | 打开任务管理器 |
| B | `Windows Terminal` | 打开终端 |
| C | `Run` | 打开“运行”窗口 |
| D | `Settings` | 打开 Windows 设置 |
| E | `File Explorer` | 打开资源管理器 |
| F | `Sound Output Panel` | 打开声音输出选择面板 |

以上只是建议，不会被程序强制绑定。

## 四、完整命令表

### 1. Windows Audio（Windows 音频）

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| Mono Audio | 单声道音频 | 切换 Windows 主输出的单声道/立体声状态，LED 显示真实状态。 |
| Clear Solo | 清除独奏 | 取消所有应用通道的 Solo，并恢复进入 Solo 前的静音状态。 |

### 2. System Tools（系统工具）

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| Task Manager | 任务管理器 | 查看程序、CPU、内存、磁盘和启动项。 |
| Windows Terminal | Windows 终端 | 打开 Windows Terminal；不可用时回退到 PowerShell。 |
| PowerShell | PowerShell | 打开 PowerShell 命令窗口。 |
| Command Prompt | 命令提示符 | 打开传统 CMD 命令窗口。 |
| Run | 运行 | 打开 Windows“运行”对话框。 |
| File Explorer | 文件资源管理器 | 打开新的资源管理器窗口。 |
| Control Panel | 控制面板 | 打开传统控制面板。 |
| System Information | 系统信息 | 查看硬件、驱动和系统详细资料。 |
| Computer Management | 计算机管理 | 打开磁盘、事件、用户等综合管理工具。 |
| Device Manager | 设备管理器 | 查看和管理硬件设备及驱动。 |
| Disk Management | 磁盘管理 | 查看分区、磁盘和卷。 |
| Services | 服务 | 查看 Windows 后台服务。 |
| Event Viewer | 事件查看器 | 查看系统和应用事件日志。 |
| Registry Editor | 注册表编辑器 | 打开注册表编辑器；修改前应谨慎。 |
| Resource Monitor | 资源监视器 | 查看更详细的 CPU、内存、磁盘和网络占用。 |
| Calculator | 计算器 | 打开 Windows 计算器。 |
| Notepad | 记事本 | 打开 Windows 记事本。 |
| Paint | 画图 | 打开 Windows 画图。 |
| Character Map | 字符映射表 | 查找并复制特殊字符。 |
| Snipping Tool | 截图工具 | 打开 Windows 截图工具。 |
| Power User Menu | 高级用户菜单 | 打开 Win+X 系统快捷菜单。 |
| Quick Assist | 快速助手 | 打开 Windows 远程协助工具。 |
| Sound Output Panel | 声音输出面板 | 打开快速设置中的输出设备、空间音频和音量混合器入口。 |
| Calendar and Clock | 日期和时钟 | 显示或隐藏任务栏日期时间面板。 |
| System About | 系统“关于” | 打开设置中的系统“关于”页面。 |

### 3. Settings（Windows 设置）

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| Settings | 设置主页 | 打开 Windows 设置。 |
| Windows Update | Windows 更新 | 直接打开系统更新页面。 |
| Sound Settings | 声音设置 | 打开输入、输出和音量设置。 |
| Volume Mixer | 音量混合器 | 打开每个应用的音量和设备路由页面。 |
| Display Settings | 显示设置 | 调整分辨率、缩放、HDR 和多显示器。 |
| Network Settings | 网络设置 | 查看网络状态和网络配置。 |
| Bluetooth | 蓝牙 | 打开蓝牙和设备页面。 |
| Installed Apps | 已安装的应用 | 查看、修改或卸载应用。 |
| Startup Apps | 启动应用 | 管理开机自动启动的软件。 |
| Default Apps | 默认应用 | 设置浏览器、播放器等默认程序。 |
| Storage Settings | 存储设置 | 查看磁盘占用和存储管理。 |
| Power Settings | 电源设置 | 调整电源、睡眠和节能选项。 |
| Date and Time | 日期和时间 | 调整时间、时区和自动校时。 |
| Clipboard Settings | 剪贴板设置 | 管理剪贴板历史和同步。 |
| Microphone Privacy | 麦克风隐私 | 管理哪些应用可以使用麦克风。 |
| Camera Privacy | 摄像头隐私 | 管理哪些应用可以使用摄像头。 |
| Accessibility | 辅助功能 | 打开 Windows 辅助功能设置。 |
| Printers | 打印机 | 管理打印机和扫描仪。 |
| Windows Security | Windows 安全中心 | 打开病毒防护、防火墙等安全页面。 |

### 4. Folders（常用文件夹）

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| Home Folder | 个人文件夹 | 打开当前用户的主目录。 |
| Desktop | 桌面文件夹 | 打开桌面文件所在目录。 |
| Documents | 文档 | 打开当前用户的文档文件夹。 |
| Downloads | 下载 | 打开下载文件夹。 |
| Music | 音乐 | 打开音乐文件夹。 |
| Pictures | 图片 | 打开图片文件夹。 |
| Videos | 视频 | 打开视频文件夹。 |
| This PC | 此电脑 | 查看磁盘、设备和常用文件夹。 |
| Quick Access | 快速访问 | 打开资源管理器主页/快速访问。 |
| Network | 网络 | 查看局域网设备和共享。 |
| Recycle Bin | 回收站 | 打开回收站。 |
| Roaming AppData | 漫游应用数据 | 打开当前用户的 Roaming AppData。 |
| Local AppData | 本地应用数据 | 打开当前用户的 Local AppData。 |
| Temp Folder | 临时文件夹 | 打开当前用户的临时目录。 |
| Startup Folder | 启动文件夹 | 打开当前用户的开机启动快捷方式目录。 |

### 5. File Explorer（文件资源管理器）

这些命令只在资源管理器位于最前面时生效。

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| Extra Large Icons | 超大图标 | 使用最大的文件图标显示。 |
| Large Icons | 大图标 | 使用大图标显示，适合图片和封面。 |
| Medium Icons | 中等图标 | 使用中等大小图标。 |
| Small Icons | 小图标 | 使用较小图标，显示更多项目。 |
| List | 列表 | 以紧凑列表方式显示。 |
| Details | 详细信息 | 显示名称、日期、类型、大小等列。 |
| Tiles | 平铺 | 以平铺方式显示文件及简要信息。 |
| Content | 内容 | 每个文件占一行并显示更多信息。 |
| New Folder | 新建文件夹 | 在当前目录新建文件夹。 |
| Properties | 属性 | 打开当前选中文件或文件夹的属性。 |
| Toggle Preview Pane | 切换预览窗格 | 显示或隐藏右侧文件预览。 |
| Toggle Details Pane | 切换详细信息窗格 | 显示或隐藏所选文件的详细资料。 |
| Previous Folder | 上一个文件夹 | 回到浏览历史中的上一位置。 |
| Next Folder | 下一个文件夹 | 前进到浏览历史中的下一位置。 |
| Parent Folder | 上级文件夹 | 返回当前路径的上一级。 |
| Search Folder | 搜索当前文件夹 | 将焦点放到文件搜索。 |
| Fit Columns to Content | 自动调整列宽 | 在详细信息视图中让各列适合内容宽度。 |

提示：`New Tab`、`Close Tab`、`Next Tab`、`Previous Tab`、`Address Bar`、
`Refresh` 和 `Rename` 等通用命令同样可以在资源管理器中使用。

### 6. Window Management（窗口管理）

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| Start Menu | 开始菜单 | 打开或关闭开始菜单。 |
| Search | Windows 搜索 | 打开系统搜索。 |
| Quick Settings | 快速设置 | 打开网络、声音、电源等快速设置。 |
| Notifications | 通知中心 | 打开通知和日历区域。 |
| Show Desktop | 显示桌面 | 显示桌面；再次执行可返回原窗口。 |
| Minimize All | 最小化全部 | 最小化所有普通窗口。 |
| Restore Minimized | 恢复最小化窗口 | 恢复刚刚由“最小化全部”隐藏的窗口。 |
| Task View | 任务视图 | 显示全部窗口和虚拟桌面。 |
| Next Window | 下一个窗口 | 按打开窗口顺序切换到下一个窗口。 |
| Previous Window | 上一个窗口 | 反向切换窗口。 |
| Minimize Window | 最小化当前窗口 | 最小化最前面的窗口。 |
| Maximize or Restore | 最大化/还原 | 在最大化与普通窗口大小之间切换。 |
| Close Window | 关闭当前窗口 | 正常请求关闭当前窗口；软件仍可提示保存。 |
| Toggle Always on Top | 切换窗口置顶 | 让当前窗口保持最前，或取消置顶。 |
| Snap Left | 贴靠左侧 | 把当前窗口贴到屏幕左侧。 |
| Snap Right | 贴靠右侧 | 把当前窗口贴到屏幕右侧。 |
| Snap Up | 向上贴靠/最大化 | 按 Windows 默认规则向上调整窗口。 |
| Snap Down | 向下贴靠/还原 | 按 Windows 默认规则向下调整窗口。 |
| Move to Next Monitor | 移到下一个显示器 | 把当前窗口移到右侧显示器。 |
| Move to Previous Monitor | 移到上一个显示器 | 把当前窗口移到左侧显示器。 |
| Project Display | 投影模式 | 选择仅电脑、复制、扩展或第二屏幕。 |
| Cast | 无线投屏 | 打开连接无线显示器的面板。 |
| Lock Computer | 锁定电脑 | 立即进入 Windows 锁屏。 |
| Snap Layouts | 窗口布局 | 打开 Windows 11 的贴靠布局选择。 |
| Snap Top Half | 贴靠上半屏 | 把当前窗口放到屏幕上半部分。 |
| Snap Bottom Half | 贴靠下半屏 | 把当前窗口放到屏幕下半部分。 |
| Isolate Current Window | 仅保留当前窗口 | 最小化/恢复除当前窗口外的其他窗口。 |
| Peek at Desktop | 临时查看桌面 | 按住命令期间临时透视桌面。 |
| Window Menu | 窗口系统菜单 | 打开当前窗口的移动、大小、最小化和关闭菜单。 |

### 7. Virtual Desktops（虚拟桌面）

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| New Desktop | 新建虚拟桌面 | 创建并切换到新的虚拟桌面。 |
| Close Desktop | 关闭虚拟桌面 | 关闭当前虚拟桌面，窗口会移到其他桌面。 |
| Next Desktop | 下一个虚拟桌面 | 切换到右侧虚拟桌面。 |
| Previous Desktop | 上一个虚拟桌面 | 切换到左侧虚拟桌面。 |

### 8. Taskbar（任务栏）

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| Pinned App 1 | 固定应用 1 | 启动/切换任务栏从左数第 1 个固定应用。 |
| Pinned App 2 | 固定应用 2 | 启动/切换任务栏从左数第 2 个固定应用。 |
| Pinned App 3 | 固定应用 3 | 启动/切换任务栏从左数第 3 个固定应用。 |
| Pinned App 4 | 固定应用 4 | 启动/切换任务栏从左数第 4 个固定应用。 |
| Pinned App 5 | 固定应用 5 | 启动/切换任务栏从左数第 5 个固定应用。 |
| Pinned App 6 | 固定应用 6 | 启动/切换任务栏从左数第 6 个固定应用。 |
| Pinned App 7 | 固定应用 7 | 启动/切换任务栏从左数第 7 个固定应用。 |
| Pinned App 8 | 固定应用 8 | 启动/切换任务栏从左数第 8 个固定应用。 |
| Pinned App 9 | 固定应用 9 | 启动/切换任务栏从左数第 9 个固定应用。 |
| Pinned App 10 | 固定应用 10 | 启动/切换任务栏从左数第 10 个固定应用。 |
| Next Taskbar App | 下一个任务栏应用 | 在任务栏图标之间向后移动焦点。 |
| Previous Taskbar App | 上一个任务栏应用 | 在任务栏图标之间向前移动焦点。 |
| Notification Area | 通知区域 | 将焦点移到任务栏右侧托盘图标。 |

### 9. Editing（通用编辑）

这些命令由最前面的软件处理。

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| Undo | 撤销 | 撤销上一步操作。 |
| Redo | 重做 | 恢复刚刚撤销的操作。 |
| Cut | 剪切 | 剪切选中内容。 |
| Copy | 复制 | 复制选中内容。 |
| Paste | 粘贴 | 粘贴剪贴板内容。 |
| Select All | 全选 | 选择当前区域的全部内容。 |
| Save | 保存 | 保存当前文档或项目。 |
| Save As | 另存为 | 打开“另存为”窗口。 |
| Open | 打开 | 打开软件的文件选择窗口。 |
| Find | 查找 | 在当前软件或页面中查找。 |
| Print | 打印 | 打开打印窗口。 |
| Rename | 重命名 | 重命名选中的文件或项目。 |
| Escape | 退出/取消 | 关闭菜单、取消选择或退出当前操作。 |
| Enter | 确认 | 等同于键盘回车。 |
| Delete | 删除 | 执行普通删除；具体行为由当前软件决定。 |
| Backspace | 退格 | 删除前一个字符或执行软件的返回操作。 |
| Next Field | 下一个输入项 | 将焦点移到下一个控件。 |
| Previous Field | 上一个输入项 | 将焦点移到上一个控件。 |
| Context Menu | 右键菜单 | 打开当前项目的上下文菜单。 |

### 10. Browser and Tabs（浏览器与标签页）

多数命令也适用于资源管理器、终端和其他带标签页的软件。

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| New Tab | 新建标签页 | 新建并切换到标签页。 |
| Close Tab | 关闭标签页 | 关闭当前标签；没有标签时软件可能关闭窗口。 |
| Reopen Closed Tab | 恢复关闭的标签页 | 重新打开最近关闭的标签页。 |
| Next Tab | 下一个标签页 | 切换到右侧/下一个标签。 |
| Previous Tab | 上一个标签页 | 切换到左侧/上一个标签。 |
| Address Bar | 地址栏 | 将焦点放到网址或路径输入框。 |
| Back | 后退 | 返回上一页面或位置。 |
| Forward | 前进 | 前进到下一页面或位置。 |
| Refresh | 刷新 | 重新加载页面或窗口。 |
| New Window | 新建窗口 | 新建当前软件的窗口。 |
| Full Screen | 全屏 | 切换当前软件的全屏显示。 |
| Zoom In | 放大 | 放大网页或文档。 |
| Zoom Out | 缩小 | 缩小网页或文档。 |
| Reset Zoom | 恢复缩放 | 将缩放恢复为 100%。 |
| Page Top | 页面顶部 | 跳到页面开头。 |
| Page Bottom | 页面底部 | 跳到页面末尾。 |
| Page Up | 向上翻页 | 向上滚动一页。 |
| Page Down | 向下翻页 | 向下滚动一页。 |

### 11. Media（媒体控制）

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| Play or Pause | 播放/暂停 | 控制当前由 Windows 接管的媒体会话。 |
| Next Track | 下一曲 | 切换到下一首或下一个媒体项目。 |
| Previous Track | 上一曲 | 返回上一首或上一个媒体项目。 |
| Stop | 停止 | 停止当前媒体；部分播放器可能不支持。 |
| System Mute | 系统静音 | 切换 Windows 主输出静音。 |
| System Volume Up | 系统音量增加 | 增加 Windows 主输出音量。 |
| System Volume Down | 系统音量降低 | 降低 Windows 主输出音量。 |

### 12. Capture and Input（截图与输入）

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| Screen Snip | 区域截图 | 打开区域截图选择界面。 |
| Save Full Screenshot | 保存全屏截图 | 截取整个屏幕并保存到“图片/屏幕截图”。 |
| Copy Active Window | 复制当前窗口截图 | 截取最前面的窗口并复制到剪贴板。 |
| Game Bar | 游戏栏 | 打开 Windows Game Bar。 |
| Screen Recording | 屏幕录制 | 开始或停止 Game Bar 录制；当前软件必须支持。 |
| Voice Typing | 语音输入 | 打开 Windows 语音听写。 |
| Emoji Panel | 表情面板 | 打开表情符号、GIF 和特殊字符面板。 |
| Clipboard History | 剪贴板历史 | 打开最近复制过的内容列表。 |

### 13. Input and Language（输入法与语言）

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| Next Input Language | 下一输入语言 | 向后切换输入语言或键盘布局。 |
| Previous Input Language | 上一输入语言 | 反向切换输入语言或键盘布局。 |
| Previous Input Method | 上次使用的输入法 | 返回之前选中的输入法。 |

### 14. Accessibility（辅助功能）

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| On-Screen Keyboard | 屏幕键盘 | 打开 Windows 屏幕键盘。 |
| Open Magnifier | 打开放大镜 | 启动 Windows 放大镜。 |
| Close Magnifier | 关闭放大镜 | 关闭 Windows 放大镜。 |
| Magnifier Zoom In | 放大镜放大 | 提高放大镜倍率。 |
| Magnifier Zoom Out | 放大镜缩小 | 降低放大镜倍率。 |
| Toggle Narrator | 切换讲述人 | 打开或关闭 Windows 屏幕朗读。 |
| Toggle Color Filters | 切换颜色滤镜 | 打开或关闭已在辅助功能设置中启用的颜色滤镜。 |

### 15. EUCON Applications（EUCON 应用）

| 界面英文名 | 中文名称 | 简单说明 |
| --- | --- | --- |
| Windows EUCON | Windows EUCON | 调出已运行的 Windows Fader Bridge for EUCON。 |
| UAD EUCON | UAD EUCON | 调出已运行的 UAD Console Bridge for EUCON。 |
| Mackie Control | Mackie Control | 调出已运行的 Windows Fader Bridge for Mackie Control。 |

这三个命令使用应用间消息，不会模拟全局快捷键，也不会启动尚未运行的程序。
因此用户以后重新录制快捷键，不会破坏 EuControl 中已经保存的按键分配。

## 五、常见问题

### 按了命令但没有反应

先确认目标窗口在最前面。资源管理器专用命令只接受最前面的
`explorer.exe` 窗口；通用编辑和浏览器命令则由当前软件决定是否支持。

### 为什么普通命令的 LED 不会一直亮

普通命令相当于键盘上的瞬时按键，只在按下期间亮灯。只有能够从 Windows
读取真实开关状态的命令，才适合让 LED 长亮，例如 `Mono Audio` 和
`Clear Solo`。

### Pinned App 的顺序是什么

就是 Windows 任务栏从左到右的固定应用顺序。改变任务栏排列后，数字命令
也会跟着新的顺序，无需修改 Fader Bridge。

### 命令会不会针对 S3 写死

不会。Fader Bridge 只向 EUCON 发布通用应用命令；EuControl 根据连接的
S3、S4、S1、Dock、Avid Control 或其他兼容表面负责分配与分页。

## 六、参考

- [英文命令清单](WINDOWS_COMMANDS.md)
- [微软 Windows 键盘快捷键](https://support.microsoft.com/windows/dcc61a57-8ff0-cffe-9796-cb9706c75eec)
