# PixPin 逆向分析报告（REA / shipped-artifact 证据）

分析目标：`C:\Softwares\PixPin`（安装目录，Inno Setup 安装产物）
分析方法：静态 PE 资源/符号/字符串证据（Ghidra 12.1.4 自动分析）、安装日志 `unins000.dat`、运行时日志 `PixPin.log`、用户数据目录探测。
分析时间：单会话，Windows 11 (10.0.26200 / 26300)，x86_64。

置信度标记：
- **observed** = 直接在工件字节/日志中读到
- **derived** = 由多个 observed 事实推出
- **inferred** = 合理推测，需运行时验证

---

## 1. 目标身份

| 事实 | 值 | 置信度 |
|---|---|---|
| 主程序 | `PixPin.exe`，PE32+，x86_64，23,662,904 B | observed |
| 主程序 SHA-256 | `6dca2e4f183c02d02cb42c0756643cf525fd4c18ad1ff93727e815558da9416d` | observed |
| 应用版本 | **3.5.5.1**（`PixPin.log` 每行 `[3.5.5.1]` 字段） | observed |
| 安装器 | Inno Setup（`unins000.exe` / `unins000.dat`，日志头 `Inno Setup Uninstall Log (b)`） | observed |
| 安装目录 | `C:\Softwares\PixPin`（`unins000.dat` 内 `{app}` 记录） | observed |
| 安装 GUID | `{506270E7-E3B5-4157-B53A-3BFFE8E418DB}` → `HKCU\...\Uninstall\{...}_is1` | observed |
| 用户数据 | `%LOCALAPPDATA%\PixPin`（存在 `Data\`、`History\`、`Temp\`、`Crashpad\`、`PixPin.log`）；卸载脚本还清理 `{userappdata}\PixPin` | observed |
| 开始菜单 | `%APPDATA%\Microsoft\Windows\Start Menu\Programs\PixPin` | observed |
| 界面语言 | `chinese simplified`（卸载日志），运行时 `App Language set to: "zh-cn"`，locale `zh-CN` | observed |
| 原始工程路径 | `D:\Code\Private\PixPinProject\PixWinCapture\Win32CaptureSample\...`（DLL 内 PDB/源路径字符串） | observed |
| 框架 | Qt5（`Qt5Core/Gui/Widgets/Network/Sql/Svg/Xml/Multimedia/MultimediaWidgets/OpenGL/PrintSupport/Qml/WinExtras`）+ C++/WinRT 2.0.240405.15 + WIL + robmikh.common + ONNX Runtime + OpenSSL 1.1 + crashpad | observed |
| 网络端点 | `https://stat2.pixpin.cn`（遥测 POST）、`https://upgrade.pixpin.cn`（升级 GET） | observed |
| 灰度 | `PixUpgrade init with grayscale value: 82` | observed |
| 右键菜单扩展 | AppX 包 `PixPinContextMenu`（卸载脚本 `Get-AppxPackage -Name 'PixPinContextMenu' \| Remove-AppxPackage`）+ `PixPinContextMenu\PixPinContextMenuExt.dll` | observed |
| 崩溃处理 | `Helpers\crashpad_handler.exe`、`Helpers\crashpad_wer.dll`、`%LOCALAPPDATA%\PixPin\Crashpad\`、`PixBugReport::PrepareCrashRestartContext` | observed |

---

## 2. 模块地图（按职责分层）

**捕获层**
- `PixWin32CaptureCore.dll` — C++/WinRT 封装 **Windows.Graphics.Capture**：`GraphicsCaptureItem`、`GraphicsCaptureSession`、`Direct3D11CaptureFramePool`；`D3D11CreateDevice` + `CreateDirect3D11DeviceFromDXGIDevice`；`IGraphicsCaptureSession2`（光标）、`IGraphicsCaptureSession3`（系统蓝框）；`robmikh.common/capture.desktop.interop.h`（桌面级 `CreateForMonitor`/`CreateForWindow` 互操作）。
- `PixWinCapture.dll` — 上层 `PixWinCaptureStatic` / `PixScreenInfoManager`：`CreateDXGIFactory1` 枚举 adapter/output、`QueryDisplayConfig`/`DisplayConfigGetDeviceInfo`、`EnumDisplayMonitors`/`GetMonitorInfoW`、`D3DCompile`（后处理着色器）、`captureToQImage`、`prepareCapture`（互斥 + 超时）、`hasCaptureChanged`、`updateRegion`。
- `PixScreenManager.dll` — 屏幕管理：优先 `PixScreenGXDI::enumScreens`（DXGI 引擎，`engineType=2`），失败回退 `PixScreenQt::enumScreens`（`QScreen::grabWindow`）；光标抓取（`GetCursorInfo`/`DrawIconEx`/`GetIconInfo`/`CreateDIBSection` → `PixCursorPixmap{pixmap,hotSpot,pixmapDevicePixelRatio}`）。
- `PixAVCodec.dll` / `PixMovie.dll` / `onnxruntime.dll` / `d3dcompiler_47.dll` / `plugins/mediaservice/*` — 媒体与推理运行时。

**UI 探测层**
- `UiSpy.dll`、`UiRegionDetector.dll` — 独立线程 `UiRegionDetectorThreadWarp`：`WindowFromPoint`、`ChildWindowFromPoint`、`GetWindow`、`GetWindowRect`、`DwmGetWindowAttribute`（DWM 扩展边框）、`GetClassNameW`、`IsHungAppWindow`、`SendMessageTimeoutW`、`IsWindowVisible`、`EnumDesktopWindows`、`AccessibleObjectFromWindow`（MSAA）；内置进程黑名单（`chrome.exe`、`msedge.exe`、`brave.exe`、`opera.exe`、`firefox.exe`、`quark.exe`、`360se.exe`、`Unity.exe`、`EpicGamesLauncher.exe`、`qgis-bin.exe`、`openshot-qt.exe`、`GoldenDict.exe`、`Snap.Hutao.exe`、`Mybase.exe` 等），用于"智能元素/窗口识别"与截图时的窗口吸附。
- `PixWindowNotify.dll` — `EVENT_SYSTEM_FOREGROUND` / `EVENT_SYSTEM_MINIMIZEEND` WinEvent 钩子（活动窗口/进程跟踪，日志中 `Explorer started detected, refresh screen manager` 亦由其驱动）。

**交互/界面层**
- `PixActionsBar.dll` — 标注工具条 `ActionsBar`（`addAdvancedButton`、`addGap`、`addGapLine`、`addLayer`、`addMoveControl`、`addVerticalWidget`、`getPosMode`、`hasHideReason`、`nativeEvent`、`iconSizeScale`、`getPixelRatio`；QSS `:/qss/ActionsBarQSS`；提示语 "Hold down the mouse to drag the annotation toolbar."）。
- `PixWidget.dll` / `PixWidget2.dll` — 通用控件（`FloatPanel`、`ButtonWithFloatPanel`、`RoundedButton`、`DotLabelWidthFloatPanel`、`PixComboBox`、`PixMsgBox`）。
- `PixStyle.dll` — `PixStyleTheme`（主题变量、`appleStyleTo`、`sigThemeChanged`）；主样式 `Main QSS loaded`；图标字体 `PixPinIconfont`。
- `PixConfiguration.dll` — 设置系统：`ConfStorage`/`ConfValue`（JSON 本地文件、`createDynamicConf`、`importConf`、`saveToLocalConfFile`）、`KeySequenceEdit`（快捷键录入，`Press shortcut key` / `Clear shortcut key`）、热键占用提示 "Setup failed, the hotkey may be occupied."、开机自启写 `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run`、读取 `HKCU\Control Panel\Keyboard\PrintScreenKeyForSnippingEnabled`、保存动作 `Save.SaveAndCopy` / `Save.AfterSaveActions`、窗口几何持久化（`ConfigurationWindowGeometry`/`NormalPosition`/`NormalSize`/`Screen`/`Maximized`）。
- `qxtglobalshortcut.dll` — 全局热键：`RegisterHotKey`/`UnregisterHotKey`/`VkKeyScanExA`/`GetKeyboardLayout`；依赖 `PixWindowNotify` 做游戏模式判定（字符串 `Game mode detected, disabling shortcut` / `Game mode exited, enabling shortcut`）。
- `PixNotification.dll` — 自绘通知/Toast（`PixNotificationWindow`、`PixToastsMessage`、`PixFeatureGuidePanel`、`PixMsgBox`、`PixNotification.NoAskAgain.%1`、`NotificationCloseType`）。
- `PixPinIcon.dll`、`PixColorPalette.dll`（取色）、`PixKeyMouse.dll`（键鼠演示/高亮）、`PixPinTutorial.dll`、`PixProgramManage.dll`、`PixDownload.dll`、`PixStat.dll`（遥测）、`PixAuth.dll`（账号/JWT，日志 `needJwtAuth`）、`PixNetwork.dll`（`PixNetworkReply`）、`PixUtils.dll`、`PixSystemUtils.dll`、`PixVision.dll`、`PixScreenManager.dll`。

**识别/公式层**
- `PixModelRunner.dll` — ONNX Runtime C API 封装（只导入 `OrtGetApiBase`）：环境/会话缓存（`sessionCacheCount`、`Using cached ONNX Runtime session`）、`CreateThreadingOptions`、`session.use_env_allocators`、`OrtDeviceAllocator`、输入/输出张量校验、**`Decrypting model data.`**（模型文件加密）、类 `PixOcrRecInfer`（OCR 识别推理）。
- `PixOCR.dll` / `PixOCR2.dll` / `PixFormulaRec.dll` / `PixLatex2MathML.dll` — OCR 与公式识别；`PixLatex2MathML.dll` 内置 LaTeX→MathML→OMML 全链路：`mathml.xml`、`mathml_normalize.xsl`、`MML2OMML.XSL`、`WordTemplate.zip`、`unimathsymbols.txt`、`latex_symbol.txt`，输出 `<m:oMathPara>`（可直接粘贴进 Word）。
- `model\` 目录 + `PixModelRunner.dll` + `onnxruntime.dll` — 本地模型推理运行时。

**辅助进程**
- `PixPinAuxiliary.exe` — 三模式：`Upgrade <TargetRootPath> <UpgradeFilePath> <ExeName>`（zip 自升级，AES `ChainingModeECB`，全局互斥 `Global\PixPinUpdater`，对话框 "PixPin is Upgrading"）、`Restart <mode> <exepath>`（`Elevated`/`Normal`）、`CrashRestart <mode> <exepath> <pid> <statepath> <logpath>`（崩溃后自动重启）。
- `Helpers\crashpad_handler.exe`、`Helpers\crashpad_wer.dll` — 崩溃采集。

---

## 3. 截图流程（运行时实测，来自 `PixPin.log`）

```
PixPinAction::onShortcutTriggered  Global shortcut activated shortcut=Ctrl+1 pendingCount=1
PixPinAction::processPendingShortcutTriggers
PixPin::screenShot  globalHotkey=true quickShot=false afterSelectAction=0
  stage=prechecksComplete → beforeCaptureComplete
ScreenShotView::prepareScreenShotBgLayer  screenCount=2
  ScreenPixmapItem::syncPixmap  geometry=-1920,0 1920x1080   (每显示器一次)
    PixScreenGXDI::grabWindow  region=QRect(0,0 -1x-1)     # 全屏抓取
      PixScreenGXDI::grabWindowImage
        PixWinCaptureStatic::getExpectedImageSize  image=1920x1080 stride=7680 bpp=4   # BGRA 帧
        PixWinCaptureStatic::captureToQImage  image=1920x1080 format=6                 # QImage::Format_RGB888
PixPin::initScreenShotWindow  (背景已冻结 → 建窗口，耗时 ~110 ms)
ScreenShotView::refreshScreenShot 阶段序列：
  activeWindowAndScenePrepared → shotInfoBarCreated(PixInfoBar/ShotInfoBar)
  → cursorAndPointInfoConfigured → shortcutTipsConfigured
  → selectionAndAuxiliaryReset → screenshotSoundHandled → mouseSyncScheduled
UiRegionDetectorThreadWarp::run  Thread exiting          # 选区结束后关闭探测线程
```

关键结论（derived）：
1. **冻结屏幕再选区**：先抓全屏作为背景层（`prepareScreenShotBgLayer`），再显示选区窗口 —— 与 Snipaste/PixPin 一致的 "freeze-then-snip" 模型，单显示器抓取耗时约 30–40 ms。
2. **预热捕获会话**：`PixWinCaptureStatic` 为每个显示器预先 `prepareCapture`（`prepared=true`），空闲约 3 秒后 `onPrepareTimeout` 释放；显示器拓扑变化（`PixScreenManager::refresh`）时销毁重建。这是低延迟热键响应的核心工程手法。
3. **帧格式**：DXGI/WGC 帧为 BGRA32（stride = 宽×4），转 `QImage::Format_RGB888`（format=6）。
4. **窗口/元素吸附**在独立线程完成，选区结束即退出线程。

---

## 4. 存储与安全

- `PixStorage::PixStorage] Secure storage initialized path=... region=cn storageSuffix=.bin2 isReady=true` → 配置/敏感数据落盘为**加密 `.bin2`**（observed）。
- `PixModelRunnerPrivate::inference] Decrypting model data.` → **ONNX 模型文件加密**（observed）。
- 遥测：`stat2.pixpin.cn` POST，body 128–359 B，`needJwtAuth=false`；`PixTrack ProfileID`、`PixDeviceInfo uuid`（36 字符，日志中已 hash）。
- 日志对敏感值做了 hash（`nameHash=`、`pathHash=`、`urlHash=`、`idHash=`）——说明其日志体系本身支持"可上传但不泄露路径/ID"。
- 代理配置：`ApplyConfigProxyToAppliction DefaultProxy """:0" ["Tunnel Listen UDP SctpTunnel SctpListen"]`（observed，具体语义 inferred：支持系统代理/UDP/SCTP 隧道选项）。

---

## 5. 功能清单（复刻规格来源）

由模块名 + 字符串 + 日志共同支撑的功能集：

1. 区域截图（热键 `Ctrl+1`）、快速截图（直接复制）、窗口截图、全屏截图。
2. 智能窗口/元素识别与吸附（`UiSpy`/`UiRegionDetector`，Alt 单击选窗口）。
3. 标注工具条（矩形、圆角矩形、椭圆、直线、箭头、折线、画笔、荧光笔、马赛克/模糊、文字、序号标记、边框、取色、撤销/重做、线宽/颜色、可拖动工具条、可换层 `addLayer`）。
4. 贴图（Pin）：置顶、缩放（滚轮）、透明度、旋转、复制、保存、隐藏全部贴图、贴图跟随窗口（`PixWindowNotify` 的 FOREGROUND/MINIMIZEEND 钩子）。
5. OCR 文字识别（本地 ONNX：`PixOCR2` + `PixModelRunner` + `PixOcrRecInfer`）、翻译、公式识别（`PixFormulaRec` → `PixLatex2MathML` → Word OMML）。
6. 滚动/长截图、GIF/视频录制（`PixAVCodec`、`PixMovie`、`plugins/mediaservice`）、Lottie（`PixLottie`）。
7. 取色器（`PixColorPalette`）、截图信息条（尺寸/坐标/颜色）、截图音效（`screenshotSoundHandled`）、光标抓取（`PixCursorPixmap`）。
8. 历史记录（`%LOCALAPPDATA%\PixPin\History`）、贴图持久化（`Data`）。
9. 设置：快捷键录入与冲突提示、开机自启（HKCU Run）、保存格式/路径、保存后动作（`Save.SaveAndCopy`、`Save.AfterSaveActions`）、主题、语言、代理、恢复默认。
10. 系统托盘 + 右键菜单扩展（AppX `PixPinContextMenu`）+ 通知 Toast + 新手引导（`PixPinTutorial`、`PixFeatureGuidePanel`）。
11. 自升级（zip + 辅助进程 + 全局互斥）、崩溃自动重启、crashpad 上报。

---

## 6. 局限与未验证项

- 未做动态插桩/抓包：`stat2.pixpin.cn` 的 payload 结构、升级包格式未验证（inferred）。
- `PixPin.exe`（23 MB）未做全量反编译（Ghidra 单会话超时），主流程证据来自日志 + 各 DLL 导入表，而非主程序伪代码。
- OCR/公式模型文件加密，权重与前后处理未提取（`model\` 目录内容未逐一分析）。
- 版本资源（VS_VERSION_INFO）存在但未解析（`inspect_pe_resources` 只做了资源目录清单，RT_VERSION payload 未解码）。
- 第三方库版本未逐一核对（Qt5 具体小版本、OpenSSL 1.1、crashpad 版本）。

## 7. 法律/道德边界

本报告仅用于互操作性学习与功能复刻。复刻实现**不复制** PixPin 的任何二进制、字符串资源、图标字体、模型权重、QSS 原文或商标；所有实现均为独立编写。PixPin 名称、图标、模型与字体不得再分发。
