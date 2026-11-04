# 拾影 / Still

C++ / Qt 6 实现的离线截图与剪贴板工具，面向 Windows 和 macOS。主窗口、场景侧栏、剪贴板列表、截图历史与偏好使用 Qt Quick/QML；截图选区、编辑器、贴图和原生辅助对话框保留 Widgets。支持截图、编辑、复制、保存、贴图、直接选择贴图中的文字，以及持久化剪贴板历史。当前本机为 Windows；macOS 实现待 Mac 编译和验收。

## 运行

Windows 构建后运行 `build/Still.exe`，部署后运行 `dist/Still/Still.exe`。启动显示剪贴板单列列表；内容侧栏切换剪贴板／已固定／截图历史，底部偏好进入四类设置，返回内容位于分类上方，截图入口位于内容区右下角。分发时保留整个部署目录及 Qt DLL、qml/、plugins/、native/；切换版本前先从托盘退出旧进程，避免单实例机制唤起旧版本。

```powershell
# 已在本地 .tools 下准备 Qt 6.8.3 / MinGW / CMake 时，构建、测试并部署
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Test -Deploy

# 使用自己安装的 Qt 开发套件（含 Widgets、Qt Quick 与 Quick Controls）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -QtRoot C:/Qt/6.8.3/mingw_64 -Test -Deploy
```

通用 CMake 构建（Qt >= 6.8，CMake >= 3.24，C++17；推荐已验证的 Qt 6.8.3）。Qt 套件必须包含 Widgets、QuickWidgets、QuickControls2，以及 Network、Concurrent；测试还需要 Test：

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/kit -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix /absolute/path/to/dist/Still
```

Windows 请使用与 Qt 开发套件匹配的 MSVC 或 MinGW 编译器；macOS 需要 Xcode Command Line Tools 和 macOS 对应的 Qt 开发套件，安装结果是 `Still.app`，含 Still 显示名称与 ICNS 图标，尚未在 Mac 构建验收。本项目不使用 Qt WebEngine。QML 模块与依赖由 `qt_generate_deploy_qml_app_script` 部署；内部应用 target 仍为 `PixClone`，放在 `src/CMakeLists.txt`，避免 QML 导入扫描进入 `.tools/`。

若本机 Qt 6.8.3 MinGW 已装 qtbase、缺少 QML 模块，可用项目已有的 aqt 补装（从项目根目录运行，Python 环境需已安装 aqt）：

```powershell
python -m aqt install-qt windows desktop 6.8.3 win64_mingw --outputdir .tools/Qt --archives qtdeclarative
```

## 使用

| 操作 | Windows 默认键 | macOS 对应键 |
| --- | --- | --- |
| 框选后编辑 | Ctrl+1 | Command+1 |
| 框选后直接复制 | Ctrl+2 | Command+2 |
| 每屏截图并贴图 | Ctrl+3 | Command+3 |
| 显示 / 隐藏全部贴图 | Ctrl+4 | Command+4 |
| 滚动长截图 | Ctrl+5 | Command+5 |
| 剪贴板图片贴图 | Ctrl+Shift+V | Command+Shift+V |
| 关闭全部贴图 | Ctrl+Shift+Q | Command+Shift+Q |
| 呼出剪贴板历史 | Ctrl+Alt+Space | Command+Shift+Space |

快捷键可在设置中更改；占用时保留旧键。清空组合键会注销该功能的全局快捷键。Windows 升级后，旧默认的剪贴板快捷键会改为 Ctrl+Alt+Space，其他自定义组合保留。

截图和编辑工具栏使用图标按钮，悬停显示当前界面语言的名称及快捷键；线宽保留数字。窗口较窄时可从“更多工具”展开其余操作。

- **截图**：先冻结所有屏幕，再拖动框选或点击吸附窗口；八个手柄调整选区。Enter 执行默认动作，Esc 取消，C 复制放大镜颜色。一次选区限于单屏。
- **编辑**：矩形、圆角矩形、椭圆、直线、箭头、画笔、荧光笔、马赛克、马赛克笔、文字、序号，支持颜色、线宽、撤销重做。`R G O L A P H M B T D` 切工具；Ctrl/Cmd+C 复制、S 保存、P 贴图、O 复制识别文字。
- **贴图**：图片上的文字自动在本机识别。鼠标拖选文字后 Ctrl/Cmd+C 复制选中部分；未选字时复制图片。Ctrl/Cmd+A 选择全部文字。拖动空白移动窗口，Alt 拖动强制移动。滚轮缩放、Ctrl/Cmd+滚轮透明度、空格编辑、右键更多操作。左键双击在当前贴图附近显示小型非模态关闭浮层，不抬高主窗口；默认“取消”，Esc／外点也取消。仅勾选“下次不再提示”并确认关闭后保存偏好；取消不保存。之后双击直接关闭，拖选文字不变。
- **编辑已有贴图**：空格打开编辑器，完成后点“应用并关闭”更新原贴图，文字位置会重新识别。
- **滚动截图**：先框选原页面的可滚动内容，再点击选区工具条的“长截图”；也可按Ctrl+5后框选。采集中选区边框持续可见且鼠标穿透，将鼠标移入原页面选区，缓慢向下滚动。右侧以固定比例预览，图宽最多160逻辑像素，随内容增长不再缩小；超过360px视口后用滚轮或PageDown回看完整长图，额外8px滚动条。位于底部时跟随新增内容，滚回查看时保留位置；蓝框标记全图、绿框标记最后成功采集的可见部分。下方工具条沿用普通截图的42px高度、32px按钮、20px图标、7×5px边距与2px间距，保留14px柔和外圆角；总宽随尺寸和只读“纵向”标签变化。工具条提供编辑、文字识别、贴图、保存、取消、复制图标，复制入口以浅银灰强调。编辑进入普通截图编辑器，其余输出复用普通截图动作；取消不输出，达到限额或自动结束仍将最后成功结果交给编辑器。仅支持手动纵向滚动，没有横向或自动滚页按钮。预览初次出现不抢页面焦点，显式交互可获取焦点，优先放在选区右侧并约束在屏幕可用边界内。静止画面等待用户，不消耗有效拼接帧数；短暂无法对齐时保留已有结果并继续等待，可暂停或稍向上滚动找回重叠，不强行拼接不可靠画面。
  Windows 10 2004及以上且DWM桌面合成可用时，使用系统 `WDA_EXCLUDEFROMCAPTURE` 排除伴随层，允许预览覆盖选区而不进入长图；这不是通用的内容安全保证，兼容条件见[微软API说明](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)。不支持排除时，边框绘制在采集像素之外，预览只放在选区外；没有安全空间则提示缩小选区。采集过程中不通过隐藏或闪烁预览避开截图。
- **剪贴板**：主窗口默认内容页，单列记录文字、HTML/RTF富文本、图片、文件路径及有限其他格式。左侧缩略图／类型图标，中间标题与最多两行摘要，右侧时间与固定。顶部页名、计数和搜索同行，类型筛选栏右侧保留更多，有粘贴目标时显示粘贴按钮。已固定从侧栏进入，暂停记录在侧栏底部。搜索框上下逐项选择，列表上下、左右均逐项。Enter 按偏好粘贴或复制，Ctrl+Enter 仅复制并保留窗口，Ctrl+Shift+Enter 纯文本，Esc 收起。失焦不自动收起。更多菜单或空格可展开预览，紧凑窗口隐藏预览。悬停记录可复制、固定或删除，列表仍支持 Ctrl+C 复制、右键菜单与 Delete 删除后选相邻条目。文件显示名称与缩短父目录，预览保留完整路径。连续工作面不包列表／预览外框，仅以细竖线分隔；更多与右键菜单统一主题，Esc 优先关闭菜单。
  从外部用快捷键打开时记录粘贴目标；鼠标切到其他应用再回到主窗口时，旧目标失效，按钮改为“复制内容”，保留窗口并提示手动粘贴，避免粘到旧应用。
- **设置**：通过侧栏底部“偏好设置”进入通用偏好、快捷键、识别与限制和剪贴板设置；内容与偏好使用不同导航，可“返回内容”。侧栏上下方向键按可见顺序切换页面，快捷键呼出剪贴板后直接搜索。右侧采用名称与说明成组、控件右对齐的单列表单，仅分节画线，无底部关闭栏。更改自动保存并生效，无需保存按钮。开关与选项立即提交；数字输入完成或点击步进按钮时提交；目录与快捷键完成输入后提交。标题右侧只显示保存中或异常反馈。失败时恢复原设置。设置页滚动到内容边界即停止。
- **语言与外观**：通用偏好直接选择简体中文 / English、浅色 / 深色 / 跟随系统（Light / Dark / System），默认中文、跟随系统。日间为银白、冷灰与石墨动作，夜间深石墨与银色焦点；侧栏与内容连成连续工作面，玻璃操作层仅轻微单层高光。Windows支持时使用DWM Acrylic实时背景模糊，失败或高对比度模式使用不透明背景。六页顶部统一收紧。保存成功后主窗口与已打开Widgets标题、菜单即时更新，画布和工具栏跟随主题；截图图像、剪贴板原文及文件名保持不变。程序生成的数量说明仅在显示时翻译。
- **历史目录与空间**：“截图历史”以缩略图画廊展示，双击继续编辑；在“存储设置”弹层选择专用空文件夹（例如 `D:/PixCloneHistory`），确认目录后在后台迁移现有历史，保存状态在弹层内可见。可关闭自动历史，或调整默认的 200 张 / 1024 MiB 上限；超出后清理最早的图片。此目录独立于手动保存目录。
- **剪贴板目录与空间**：在“剪贴板设置”页可另外选择 D 盘专用空目录，确认目录后自动迁移已有条目。默认保留 200 条 / 256 MiB，优先清理最早的非固定条目；新内容必然放不下时保留原历史并提示。文件条目仅记录路径，重放使用复制方式，不重放旧剪切操作。详细边界见 `docs/CLIPBOARD.md`。

## OCR 与系统权限

Windows 使用系统 `Windows.Media.Ocr`，在后台通过 PowerShell 返回文本与词级位置框，需要系统安装对应 OCR 语言包。macOS 使用 Apple Vision，通过 Objective-C++ 与 Qt 接入；截图需要系统屏幕录制授权。图像和文字不会上传。

文字选择的最小单位取决于 OCR 返回的位置框，**不保证所有语言都能逐字选择**。旋转或编辑后的贴图会清除旧文字并重新识别，避免坐标错位；马赛克后的图片按实际可见内容重新识别。

OCR 最多同时运行两个任务；同一窗口再次识别会替换旧请求，关闭窗口会释放尚未开始的识别图片。已进入系统识别的任务在返回后释放图片。内存修复与验证证据见 `docs/MEMORY-FIXES.md`。

## 数据与迁移

品牌改为拾影 / Still 后，内部 target、QML URI、组织名、实例标识和数据目录仍沿用 PixClone，以兼容旧设置与历史。Windows 新安装的数据目录为 `%LOCALAPPDATA%/PixClone`；若旧 Electron 数据只存在于 `%APPDATA%/PixClone`，自动沿用该目录。macOS 使用 Qt 的应用数据目录。可用 `PIXCLONE_DATA_DIR` 环境变量指定独立数据目录，测试和部署检查使用该方式隔离真实数据。

- `qt-settings.json`：Qt 设置，原子写入；没有 Qt 设置时读取旧 `settings.json` 中可复用的字段。旧设置文件不被改写。
- `appearance.language` 为 `zh_CN` / `en`，`appearance.theme` 为 `system` / `light` / `dark`；缺省分别为 `zh_CN` / `system`。英文词条见 `assets/en.json`，由 `QTranslator` 加载，QML 使用 `qsTranslate` 与引擎重翻译，不迁移用户内容。
- `History/*.png`：默认历史目录，兼容读取旧 PNG；设置 `history.dir` 后使用指定目录，配置文件仍保留在应用数据目录。Qt 版本按文件扫描，不依赖旧 `history.json` 索引。
- `Clipboard/index.json` 与 `Clipboard/*.clip`：默认剪贴板目录；设置 `clipboard.dir` 后使用独立目录。内容只保存在本机，未加密，可暂停记录或清空；识别到应用提供的“不记录”标记时跳过，不保证所有密码来源都会提供此标记。
- 剪贴板缩略图最多128×96px，PNG不超过17000 bytes，base64小于24000字符；保持索引最多2000条、读取上限64 MiB，原始图片与格式数据不改写。
- 原交接文档保存在 `docs/HANDOVER-ELECTRON.md`；当前说明见 `docs/HANDOVER.md`。

## 验证

CTest 包含图像/存储逻辑、历史队列与迁移、贴图与选区交互、设置持久化，以及真实系统 OCR/热键集成测试。核心覆盖 DPI 裁剪、逐像素拼接、撤销重做、保存失败保护、文字跨行/反向选择、缩放后的选字以及过期 OCR 结果隔离。历史迁移、写入时序和失败保护说明见 `docs/HISTORY-STORAGE.md`。

```sh
# 仅运行不依赖真实系统 OCR 的测试
ctest --test-dir build -C Release -LE integration --output-on-failure
# 真实系统 OCR / 全局热键测试，需要可用的系统 OCR 语言包
ctest --test-dir build -C Release -L integration --output-on-failure
# 检查英文翻译覆盖、占位符和重复键
python scripts/check-translations.py
```

## 当前限制

- macOS 代码已接入，但尚未在 Mac 编译或测试；不能视为已经交付可用的 Mac 安装包。
- QML 已验证键盘与可访问名称，尚未进行屏幕阅读器实测、长期内存或 GPU 性能基准。
- 剪贴板不备份文件本体，不支持邮件虚拟附件；其他格式按有限 MIME 数据尽力保存，不保证所有软件的私有格式可跨进程或重启恢复。Windows 无法激活原窗口、权限不匹配或 Mac 未授权辅助功能时，使用“仅复制”后手动粘贴。
- 滚动长截图仍是 Beta，固定表头、动画、虚拟列表或一次滚动过远仍可能无法对齐，需要暂停或向上回滚找回重叠；应只框选滚动内容，避开固定栏。不支持系统捕获排除时，选区外需留出预览空间，否则提示缩小。当前预览验证限于Windows 11单屏测试环境；旧Windows、Mac、多显示器与目标网站尚未验收。
- 历史后台队列最多保留 8 张、合计 256 MiB 未压缩图片；磁盘繁忙超出队列预算时会提示本次未入历史，仍可手动保存。长截图输出在分配前受高度和 256 MiB 未压缩字节预算限制；拼接仍需同时持有输入与输出图片。
- 截图使用 `QScreen::grabWindow`，不是自建 WGC / ScreenCaptureKit 帧池；尚未做响应延迟、HDR 或长期内存基准。
- 本轮不迁移旧版的开机自启、快门音、文件名模板和可选 Tesseract 引擎。旧设置字段保留，但未接通的字段不展示为可用功能。
- 贴图在当前会话内保留；重启后可从历史重新打开，未实现窗口布局恢复。

应用Logo为项目自绘几何SVG（`assets/still.svg`），保留原轮廓，石墨底、白色取景框与一页、银色角。`scripts/generate-logo.ps1`在Windows导出PNG、七尺寸ICO和ICNS；来源与用途记录在 `assets/still.png.json` 出处sidecar，无新增AI、stock或第三方素材；不宣称位图内嵌了出处元数据。

仅参考 PixPin 的行为与交互结论；不复用其二进制、图标字体、模型、样式或商标。分析来源见 `docs/REA-FINDINGS.md`。
