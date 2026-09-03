# Apollo Bridge for EUCON · 底层功能与旧调试界面说明

> 本文保留通道功能、开发验收和旧调试面板的历史说明。新版日常界面、权限、
> 音量上限、后台运行和登录启动请使用 [UAD Console Bridge 使用指南](UAD_CONSOLE_BRIDGE_GUIDE_ZH.md)。
> 下文的逐通道解锁按钮、旧目录和解锁时音量上限指旧调试界面，不是新版主界面。

这是独立的 Apollo 桥接程序，当前阶段为**通道与控制室验证版**，还不是可用于正式录音工作的完整控制器。默认只读，需要分别开放指定通道和控制室。它不会替换 Windows Fader Bridge 的两个版本；Mackie 版后续单独制作，共用 Apollo 通信和通道模型。

## 目前能做什么

- 读取本机 UA Mixer Engine，自动识别在线设备、有效输入和 AUX。
- 显示通道名称、Mono/Stereo 格式、原生 dB 增益、静音、独奏、双声像，以及分开的实时信号与峰值保持读数。
- 显示监听音量、Mute、Dim、Mono 状态；通过独立的 EUCON 标准控制室模型提供控制。**主监听不占通道、不出现在推子列表中，普通通道推子无法调节主监听。**
- 断线清空旧状态，重连重新读取；保留逻辑通道身份，名称变化不变成新通道。
- 可手动启用标准 EUCON 通道反馈，供 S3 和 Avid Control 验证。EUCON 负责硬件发现、分配和翻页。

**支持多通道独立控制。** 点击列表中的通道，再点“开放所选通道”，可以陆续开放多条；“开放全部通道…”仅开放当前已在线且支持的通道。选择其他行不会转移权限。可以锁定所选通道或全部通道；新上线的通道不会擅自解锁。已经送达声卡的操作无法撤销，不要将验证软件当作监听紧急静音开关。

推子和 PAN 触摸期间不反向推动对应控制。连续操作按通道和参数合并，实际值由声卡回读确认。输入源、输出路由、单双声道、48V 或插件拓扑变化后，程序丢弃旧页面手势并按完整新状态重建；同一稳定逻辑通道保留用户已经授予的权限，不要求反复解锁。断线、声卡身份变化、通道消失、过期数据或确认失败仍停止写入。启动、重连和开放权限本身不改变音频状态。

目前支持已识别的 Mic、Line、Virtual、ADAT、S/PDIF 输入、AUX 返回和明确识别的主 TALKBACK 输入条。未知类型、无法确定主设备的对讲仍保持只读。只开放实际存在且可用的参数。控制室权限独立，开放通道不会顺带解锁主监听。

本轮新增功能的真实写入与控制面操作仍待验收。主监听试听 Cue 的来源选择与通道到 Cue 的发送分别位于 MON 和 AUX。默认不会改变参数；声卡内部配置写入需在设置中明确授予 CONFIG 权限。不提供音箱校准。

### 通道功能在哪里

| EUCON 区域 | 功能 | 操作与限制 |
| --- | --- | --- |
| AUX | 通道到 AUX/Cue 的发送量、开关 | 旋钮调 dB；下方 In 开关控制发送开/关，亮表示发送开启。目标名称来自实际引擎，不固定成六路。 |
| AUX 子页 | 发送 PAN | 按旋钮进入，Back 返回。只在单声道发送具有此能力时出现；立体声通道不显示无效 PAN。 |
| MIX | 轨道输出目的地 | 按对应下方开关选择，旋转不改路由。回读确认并完整刷新后自动继续本通道控制。 |
| Input：话放输入 | 48V、HPF、Gain、Phase、Pad | 48V/Pad 仅在 Mic 且非 Hi-Z 时出现。普通 Gain 为 10–65 dB 与设备范围交集；Line Gain Bypass 或 Unison 下不猜增益曲线。 |
| Input：纯线路输入 | +4 dBu / -10 dBV | 对应线路参考电平，灯显示当前选择；不是话放的 20 dB Pad。仅显示硬件提供的控制。 |
| Input：S/PDIF | SRC | 开启/关闭输入采样率转换；作用于左右两路，不是两个独立开关。由引擎决定是否可用。 |
| Input：AUX 返回 | PRE / POST、MONO | PRE/POST 作用于这条 AUX 总线的所有输入发送。MONO 合并返回信号，物理总线仍为立体声。MUTE 用正常静音键。 |
| Input：TALKBACK | TALK、TB to Mon | TALK 为按一下开/关；TB to Mon 决定是否送到主监听音箱，有啸叫风险。标准控制室 TALK 保留其短按/长按语义。 |
| Input 子页 | Mic / Line 输入选择 | 按 Input 旋钮进入，再按下方开关选源；Hi-Z 接入或 48V 已开启时不开放切换。成功切换后自动接续。 |
| 通道 Rec 与灯 | UAD REC / MON | 应用户要求复用：灯亮=插件效果录入 DAW，灯灭=监听有效果、DAW 录干声。不是录音待命或正在录音。AUX 不提供此切换。 |
| 上部自定义 Console 页 | UAD REC / UAD MON | 同一设置的明确文字入口，可通过 EUCON 顶层功能分页找到；与通道 Rec 同步。该快速控制页本轮未重排。UNISON 和 AUX 效果始终录入，不受此开关影响。 |
| 上部 Channel Control | Inserts、Input、AUX、Pan、Mix | 与下部混音区共用同一套页面、参数、回读和通道权限，不是复制的数据。选中通道后，从上排顶层按对应旋钮进入；Page 翻页、Back 返回。EQ、Dyn、Group 没有匹配的 UAD 通道语义，不提供参数页；设备可能仍显示原生类别名称。 |
| Inserts Config | 插件装卸与预置 | Config 进入插槽配置后，第一格 `NONE` 可随时卸载插件并释放 DSP；其余每格是一类插件。转动分类旋钮浏览该类插件，按下方 `In` 确认加载；分类超过一页时使用 Page。仅浏览不会写入。S3 OLED 使用最多 8 字符的硬件短名：自动去掉 `UAD`／`UADx` 前缀，并优先保留型号、版本或音色标识；写入 UAD 时仍使用完整原名。已加载插件页的 `In` 控制该插件启用/旁路，LED 始终以 UAD Console 回读为准。 |
| UNISON Config | UNISON 插件装卸与预置 | 进入上部自定义 UNISON 页后按 Config：第一项 `NONE` 明确卸载；其余只显示 UAD 当前原生标记为 UNISON 且已经授权的插件，绝不靠名称猜测，也不列出未购买插件。旋转预选、按下方 `In` 确认。已加载 UNISON 插件可在其参数页按 Config 调用该插件报告的现有预置。 |
| 上部自定义 Control Room | Level、Mute、Dim、Dim dB、Mono、Talk、Monitor/Cue 1–4 | 不占推子通道；共用独立的控制室权限和监听音量上限。开放普通通道或 UNISON 不会解锁控制室。来源名称是友好显示，写入仍使用引擎的精确来源值。 |
| 上部自定义 UNISON | 专用 UNISON 插槽的插件开关和实际参数 | 与普通 Inserts 完全分开；显示插件和参数的实际名称、离散选项、工程值并自动分页。没有插件时显示 None。Line Gain Bypass 时保留插件名称但不开放控制。插件替换、输入源变化或参数定义变化会取消旧手势。UNISON 始终进入录音路径，不受 UAD REC/MON 控制。 |
| Inserts | 已加载的插件及开关 | 保留实际插槽顺序；空槽显示槽位编号，例如 `1 None`、`4 None`，无可操作子页。已加载但关闭的插件仍显示插件名称。下方 In 灯亮表示插件开启。 |
| Inserts 子页 | 插件连续参数和离散选项 | 显示参数名及引擎提供的值；离散项显示实际选项。EUCON 自动分页，Back 返回。没有可靠显示值时暂以百分比显示，不冒充 dB/Hz。 |

各控制面的展开和导航方式由 EUCON 决定；不需要为了这些功能改写 S3 的硬件布局。发送、前级和插件同样受当前通道授权约束，开放通道不会连带开放控制室。

Group 是 EUCON 的分组总线/控制组功能，不是按颜色分类，也不是立体声链接；目前不伪造组。EQ/DYN 暂不实现，未经元数据确认的 UNISON 增益曲线、Apollo 硬件 Gain Stage Mode、立体声链接、ALT Trim 和未经确认的附加监听输出仍留在 UAD Console。

### 颜色与敏感操作

话放输入为纯绿（#00FF00）、纯线路为纯橘（#FF8000）、S/PDIF 为纯蓝（#0000FF）、ADAT 为青（#00FFFF）、虚拟通道为纯红（#FF0000）、AUX 为深紫（#8000FF）、TALKBACK 为纯黄（#FFFF00）。分类依据通道能力，不受自定义名称影响；带话放的通道切换为 Line 源时仍属于话放类。未知类型保持中性灰，不凭名称猜测。

底部按钮没有闪烁提示功能，只在状态变化时更新。“操作超时，未发送”表示该手势在实际发送前已超过 500 毫秒，程序放弃这次指令并锁定对应通道或控制室，避免旧指令延迟执行。重新开放对应控制后再操作；这不代表 UAD 已执行该指令，也不是 UAD 返回的故障。若反复出现，请保留日志用于排查延迟。

48V **开启**、TB to Mon **开启**以及已送主音箱时开启 TALK，默认受保护。先开放所选通道，再点“敏感操作保护…”确认。确认本身不会发出任何音频指令；解除保护只限所选通道的本次授权，锁定、路由/幻象上下文切换或断线后恢复。关闭 48V/TALK/TB to Mon 不需要额外解除保护，但仍需有效通道权限。请先确认麦克风支持 48V，并降低监听音量、确认不会形成声学反馈。

### 回来后集中测试

UNISON 的全部写入（包括插件开关）也需要“敏感操作保护…”确认，因为插件参数可能联动模拟增益、阻抗或幻象供电，不能仅靠参数名称判断风险。解锁 UNISON 不会解锁控制室。

当前上部导航测试版位于 `artifacts/apollo-cr-label/Release/ApolloBridge.Eucon.exe`。修正了初次实机反馈中 `INPUT` 与 `Input` 重复的问题：标准 Input 页面只绑定其对应的顶层入口，不占 EQ 位置，也不添加到自定义区。功能入口显示统一大写，包括 INSERTS、INPUT、AUX、PAN、MIX、UNISON、CONSOLE。控制室的 4 字符和 8 字符短名称都为 CR，完整名称保留 CONTROL ROOM，由 EUCON 选择合适的文字版本。第二组的 UNISON（9）、CONSOLE/快速控制（11）和控制室（16）位置及内容不变；通道名和插件原名不强制改写。

CR 短名称版本的 Release 构建成功，Release、Debug 各通过 14,738 项纯测试；前一版绑定修正已通过模拟通信回归。本次修改入口的三档显示文字并增加回读校验，没有重跑通信测试，也未启动新版进行 SDK 运行和 S3 / Avid Control 实机验收。旧版输出保留。切换前应先退出旧 Apollo Bridge，避免两个同名 EUCON 模型同时运行。

1. 先保持只读，同时在 S3 与 Avid Control 核对顶层只有一个 Input 入口，按下后进入正确的输入页；EQ、Dyn、Group 不应打开 Input。再核对 AUX、MIX、Inserts 的名称、插槽、分页、返回，以及 UNISON / Console / Control Room 的位置；确认 MONITOR 仍不占推子。
2. 降低监听音量，开放一个安全测试通道。在 Console 对照检查发送量、In 灯及单声道发送 PAN；立体声通道不应出现发送 PAN。
3. 开放两条安全通道，切换其中一条的输出路由，确认 MIX 灯、输出格式正确，刷新后继续控制，另一条不受影响；手动恢复测试前路由。
4. 分别检查话放、纯线路、数字、AUX 与 TALK 的 Input 项目；测试 Rec 灯与 Console REC/MON 的双向对应。48V 和 TB to Mon 只有操作者确认条件安全后才测试，不自动测试。所有改动由操作者手动恢复。
5. 用现有插件验证开关、连续参数、离散选项及原生显示值。在 Console 更换插件后，旧参数页必须失效，不能继续控制新插件，重新开放后再测。
6. 各旋钮反复触摸/放手、进入/退出子页、换通道与翻页，检查没有卡住的触摸状态、反馈振荡或脱离；最后回归推子、双 PAN、电平和既有控制室功能。

新增功能全部经过纯逻辑与模拟引擎测试，但这不替代真实硬件验收。UA 私有协议不提供原子事务，测试时不要在 Console 和控制面上同时改同一个参数。

### 独立控制室

1. 在 Console 确认当前监听音量适当，再点控制室卡片的“解锁控制室…”并确认。解锁本身不改变任何音频参数。
2. **解锁时的实际监听音量成为本次上限**，显示在卡片中。可以调小，往大调只能到这个上限；这不是校准声压或听力安全保证。
3. 使用 EUCON 控制室的主音量旋钮、Mute、Dim，以及折叠格式中的 Mono。Mono 关闭即恢复原立体声混音，不改变物理输出格式。Mute/Dim 使用按一下切换的锁存方式，不会因松手自动取消。
4. DIM Amount 使用标准 DimLevel 旋钮，显示 -60、-51、-43、-34、-26、-17、-9 dB 七档；内部转换为 UA 的正衰减量。只对识别到该能力的设备开放。
5. 主控制室 SOURCE 中选择 Mix 或 Cue 1–4（只显示引擎明确提供的选项）。这只是选择工程师正在试听的混音，不改表演者的 Cue 音量或发送。
6. TALK 使用标准 Talkback 开关：短按保持，长按释放，时间阈值由 EUCON 控制端决定；软件不另造按键计时。须识别到当前在线主设备的原生对讲麦克风。对讲发送和自动 DIM 由 Console/Apollo 负责，程序不改变发送目标、麦克风选择或增益。启用了 Talkback to Monitor 时，拒绝从桥接程序打开 TALK，避免啸叫。
7. **解除 Mute/Dim、减小 DIM 衰减量或切换来源都可能明显变响；TALK 会打开麦克风。** 第一次测试请确认 Console 的监听音量、对讲目的地与发送量，不要同时在两端改同一参数。
8. 点“锁定控制室”或在设置中切换只读才停止后续写入。断线、设备状态/能力/对讲配置变化或回读失败只丢弃本次旧操作；用户已选择的权限保持有效，并在最新状态可用后自动重新绑定。主监听上限只限制桥接器发出的音量目标，不阻断 MUTE、DIM、MONO、监听源和 TALK。不会自动重试不确定写入或恢复旧值。**连接丢失后无法保证已开启的 TALK 自动关闭，必要时请用 Console 或硬件关闭。**

仅允许唯一、明确报告为主音箱/STEREO/普通输出增益模式、来源为主混音或明确枚举的 Cue 的监听区；缺少必要元数据或不支持的配置仅供查看。校准增益和音箱选择不可修改。上限和权限不保存到下次启动。以后需要更高上限，应先锁定，在 Console 由操作者调整并确认，再重新解锁。

### MON A–D、COMS 与 ALT Trim

- **MON A–D** 是四组附加监听输出的标准控制，可各有音量、源选择、静音和 Dim。不是四个主监听源按钮，不能因 Apollo 有四路 Cue 就强行一一对应；仅在确认真实附加输出能力后映射。
- **TALK / Talkback** 是控制室向表演者说话；**COMS / Listenback** 是从录音间返听到控制室。当前未确认 Apollo 有独立 Listenback 功能，因此不创建 COMS。
- **ALT 1 TRIM** 是备用音箱电平校准，不是 Monitor A 音量。当前标准 Monitor 布局没有对应的独立 ALT Trim 旋钮，不借用其他控制或创建假通道；继续在 Console 中设置。

参考：[UA 控制室说明](https://help.uaudio.com/hc/en-us/articles/25351900668564-Monitor-Column)、[UA Talkback](https://help.uaudio.com/hc/en-us/articles/26489966354836-Talkback)、[Avid S4/S6 控制定义](https://resources.avid.com/SupportFiles/ProMixing/S4_S6_Guide_v2025.12.pdf)。

硬件控制位置由 EUCON 决定，不接管 S3 自带音频接口的 Audio Control 功能，也不保证每台控制面都具有独立控制室旋钮。新模型需要在 S3 与 Avid Control 的监听页面/可分配控制中分别核对。

## 构建

需要 Windows 11 x64、Visual Studio 2022 的 v143 C++ 工具和 Windows SDK。
EUCON 程序还需要从 Avid 单独取得 EUCON SDK 2026.4，以及匹配的运行环境。
仓库不提供 Avid SDK、厂商示例、手册、安装包或任何厂商二进制。

```powershell
# 构建并运行纯逻辑测试；不需要 Avid SDK，也不联网
.\scripts\build-apollo.ps1 -CoreOnly

# 另外运行模拟引擎测试：仅使用临时本机端口，不接触真实 UA 引擎
.\scripts\build-apollo.ps1 -CoreOnly -TransportTests

# 构建独立 EUCON 程序；不会自动启动或接管硬件
.\scripts\build-apollo.ps1

# 独立构建本轮待验收版本，不覆盖现有运行版
.\scripts\build-apollo.ps1 -TransportTests -NativeOutputName apollo-console-workflow
```

程序位于 `artifacts/apollo-eucon/Release/ApolloBridge.Eucon.exe`。
Debug 输出在独立的 Debug 子目录。现有 EUCON、Mackie 输出不会被覆盖。

## 使用与验证

1. 保持 Apollo 和 UA Mixer Engine 正常运行，打开新的 Apollo Bridge 程序。
2. 程序自动只读连接本机引擎。先对照 Console 检查通道数、名称、dB、双声像和监听状态。
3. 点右上角切换中文/English。鼠标滚轮或右侧滚动条浏览全部通道。
4. Windows Fader Bridge for EUCON 可以同时运行；两款程序使用不同的 EUCON 应用与持久标识。准备硬件测试时只需退出重复的 UAD 桥接实例或 EUCON SDK 示例。
5. 点“连接 EUCON”，再在 EuControl 中选择 Apollo Bridge for EUCON。此时仍然只读。不要同时运行其他处理端 EUCON 示例程序。
6. 同时在 S3 和 Avid Control 核对反馈：名称、顺序、翻页、Mute/Solo LED、双声像、触摸/放手后的马达以及电平。
7. 在桌面列表选择一个安全测试通道，再点“开放所选通道”。先以静音、低电平条件验证音量、左右 PAN；再由你手动验证 Mute/Solo。对照 Console 检查双向反馈，并分别在 S3 和 Avid Control 验收。别在同一参数上同时操作两端：私有协议不提供原子事务。
8. 控制室需要单独解锁，按上方说明验证；确认普通通道列表中没有 MONITOR，推子不能改变监听音量。
9. 发现错误时分别点“锁定全部通道”和“锁定控制室”，再打开日志目录。已经送出的操作无法撤销。退出新程序即可结束验证；不会修改或重启 EuControl、驱动或旧桥接程序。

“EUCON 模型已注册”仅表示应用模型注册成功，**不代表硬件已附着或电平可见性已验证**。设备反馈验收必须以实际 S3 和 Avid Control 为准。验证版暂不提供开机启动或托盘常驻。

推子使用本项目定义的 dB 行程分配，不声称与 Console 图形推子的行程相同。电平值以引擎的原生 dB 为准。立体声的左右声像独立保留，不合并成 Windows 式平衡。

### 双 PAN、声道格式与主输出

- **单声道**：提供一个 PAN。**立体声**：同一旋钮位置提供 `Pan L` / `Pan R` 两个独立参数，通过 EUCON 标准上方副功能开关切换；在 S3 上对应旋钮旁的 **Sel**，不是推子旁的通道 Sel。切换由 EUCON 完成，不能将左右位置平均成一个值。左右旋钮具有原生 PAN 语义；仅在该通道开放控制后写入声卡。Sel 本身只切换显示，不改变 PAN。
- **格式**：Mono/Stereo 描述通道本身和电平表腿数；单声道通道也可以声像定位到立体声监听总线。两者分别上报。监听的 Mono 合并开关不表示物理输出变成一个声道。
- **控制室**：不属于输入/AUX 通道列表。监听衰减只通过独立 Monitor processor 的 ControlRoom 旋钮提供，不存在主监听通道推子、Solo 或 PAN。
- **电平**：`GAIN dB` 是增益/衰减；`LEVEL dBFS` 是当前信号，`PEAK dBFS` 是引擎给出的峰值保持。桌面数值取已映射声道中较高的一侧，EUCON 分别传送每一侧；缺失值显示 `—`，不凭空生成峰值。引擎的底限读数也保留，不伪装为负无穷。
- **主输出表**：按 UA 的监听总线表语义读取，不扣除监听音量衰减。不能根据音箱音量推断总线电平。除明确的立体声配置外，不猜测多声道扬声器排列。
- **S3 刻度**：遵循 EUCON Meter API 3.1 的 dB 语义，不为某台设备灯柱上的印刷刻度增加或减去固定偏移。

日志位于 `%LOCALAPPDATA%\Apollo Bridge\EUCON\logs`。记录返回码、事件类型、线程、临时通道标记及电平可见性，不记录设备原始硬件标识或完整插件状态。日志应留在本机，分享前自行检查。

## 命令行检查

```powershell
# 不连接引擎，不初始化 EUCON
& .\artifacts\apollo-eucon\Release\ApolloBridge.Eucon.exe --smoke-test | Out-String

# 明确执行 24 秒真实只读观察；不初始化 EUCON，不影响现有 EUCON 程序
& .\artifacts\apollo-eucon\Release\ApolloBridge.Eucon.exe --observe-seconds 24 | Out-String

# 先退出 Apollo Bridge 和其他 EUCON 测试程序，再显式运行 SDK 标签回归
# 只初始化 SDK，不注册应用模型，不连接 Apollo，不打开音频或 MIDI
& .\artifacts\apollo-console-workflow\Release\ApolloBridge.Eucon.exe --sdk-text-test | Out-String
```

普通构建和 CI 不会执行真实引擎观察。`--observe-seconds` 才会主动连接本机 4710 端口，最长 60 秒。界面启动也会进行同样的只读观察，但 EUCON 连接需要手动开启。

`--sdk-text-test` 是显式诊断选项，不会被普通 smoke 或纯逻辑测试调用；需要本机 SDK 运行环境。除了标签回归，现在还检查各类通道的真实 SDK 页面构建、Rec 状态对应和局部页面更新；使用虚构模型，不注册节点、不连接 UA。各诊断模式不能混用。

`scripts/inspect-apollo.ps1` 是开发用只读树探测工具。完整采集必须显式指定仓库以外的私有目录；不要提交真实设备树、序列号、Console 会话或厂商配置。

## 验证范围与下一阶段

当前自动测试覆盖 JSON/UTF-8、分帧、严格写入范围、通道身份、在线过滤、双声像、异常数据、dB 表、连续操作合并、权限代次、模拟断线重连、错误回读和禁止重试；另有控制室独立锁、音量上限、监听上下文变化和禁止通道授权越界测试。真实 x8 的 BUS 1/2 音量及双 PAN 已完成小幅写入/回读/恢复，Mute 和 Solo 没有自动实测。用户已接受普通通道阶段，并确认既有控制室在 Avid Control 中可用、S3 的 Mute/Dim/Mono 软键可用。**新增 DIM Amount、SOURCE 和 TALK 尚需用户验收；自动测试不改变真实监听音量或打开麦克风。**

之后依次完成：

1. S3 + Avid Control 的独立控制室模型验收；回归通道列表、Meter API 3.1、PAN 和翻页。
2. 完善通道控制权限和控制室操作体验。
3. 用户已确认发送和插件参数控制基本正常；本轮须验收新增 Input 项目、Rec 映射、分类颜色、多通道控制和路由衔接。48V/TALK 的实际操作仅由用户在安全条件下完成。

协议来自 UA 的私有接口，并非受支持的公开 SDK；升级 UA 软件后需要重新验证。
详细依据和未确认事项见 [研究记录](APOLLO_RESEARCH.md)。

## Config 实验版（默认不启用）

实验前的本地回退提交为 `7de5f97`，未推送。用以下方式启动隔离输出：

```powershell
& .\artifacts\apollo-config-expanded\Release\ApolloBridge.Eucon.exe --experimental-config
```

先退出其他 Apollo／EUCON 适配程序。窗口标题含 `CONFIG EXPERIMENT`；连接 EUCON 后，
在上排进入 INPUT、MIX、CR 或 CONSOLE，再按 **Config**：

- INPUT：对应通道可用的输入源、参考电平、SRC、48V、AUX PRE/POST/MONO、TB TO MONITOR。
- MIX：该通道已有的输出目标，仍需明确按键选取，不会旋转即改路由。
- CR：保留 DIM 深度与监听来源；新增各 CUE 的 MONO、MIX/CUE 来源、MIRROR 输出，
  以及各耳机的 CUE 分配。只列出引擎当前允许的目标，不抢占被占用的输出。
- CONSOLE：采样率、时钟、缓冲、延迟补偿、CUE 数量、数字镜像、FCN 功能开关分配、HEADROOM、
  Console PRE/POST-FADER 计量、峰值／削波保持、Controls Mode、MIDI 输入设备。
  只提供当前接口实际报告的选项；截图之外的旧状态项已移除。
- INSERTS → Config → 进入插槽 → PLUGIN：旋转浏览已授权插件，按 In 加载；NONE 为明确卸载。
  未授权插件不列入，不会启动试用。
- 进入常规 INSERTS 中已加载的插件 → Config → PRESET：浏览该插件报告的已有预置，按 In 调用。
  暂不保存、改名或覆盖预置，也不把全局 Plug-In Scenes 冒充单插件预置。
- UNISON → Config：第一项 NONE；其余只列出同时报告 `UNISON` 能力和已授权状态的插件。
  加载方式与 INSERTS 相同；进入已加载的 UNISON 插件后再按 Config 可调用其已有预置。

CONFIG 写入权限在程序设置中单独开放；S3 上只显示实际声卡配置，不再显示含义不清的 `ACCESS` 状态格。
旋钮只预选，按对应的 **In（下方按键）** 才写入；10 秒不确认会取消预选。
现有 INPUT/MIX 和 CR 的普通控制继续使用原来的通道／控制室权限，CONFIG 权限不会解锁它们。
改变插件结构、路由或声卡能力会丢弃旧手势；已在桌面确认的权限会在最新状态上自动续接。

请先降低监听、耳机音量并停止录音：采样率／时钟可能中断音频；HEADROOM +24 比 +20
提高模拟输出参考电平；镜像和 CUE 路由会改变实际输出去向。每次配置写入都重新检查目标
身份、当前值、可用选项，并核对完整节点的回读。失败或结果不明不会自动重试，先到 Console
核对实际状态再操作。断线或设备状态变化会取消当次配置操作；权限在最新状态恢复后继续有效，已发送但未确认的命令不会自动重试。

以下暂不提供：ALT 数量（未确认单一原子接口）；MIDI 的通道／事件／音符编号（目前只找到
Console 本地偏好）；预置保存／覆盖。不会在线改写 UA 的配置文件来绕过接口。
Apollo x8 的前面板 **METER IN/OUT** 被官方手册明确列为 Console 无法远控的功能；
它与这里的 PRE/POST-FADER 不是同一项，因此不做替代映射。

普通页、固定发送目标与现有快捷控制保留不变。S3 的右侧 Page 两键同按是本机 I/O，
不是这里的 Config。新增内容仍属于私有接口实验：自动模拟测试和只读发现不能替代真实
加载／预置／路由验收；需由用户在安全条件下检查 S3 和 Avid Control 的显示、确认及回读。

实现约束及验证记录见 [Config 实验说明](APOLLO_CONFIG_EXPERIMENT.md)。
