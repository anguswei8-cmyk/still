# 拾影 / Still Qt 交接

更新日期：2026-10-31。当前主实现为 C++17 / Qt >= 6.8，版本 0.2.0。主窗口、侧栏、剪贴板与五类设置使用 Qt Quick/QML；截图选区、编辑器、贴图及原生辅助对话框保留 Widgets。

## 接手

1. 看根 `README.md` 的使用、构建和限制。
2. Windows 本机工具链位于 `.tools/`，运行 `scripts/build.ps1 -Test -Deploy`。Qt 套件需含 Widgets、QuickWidgets、QuickControls2；已有 qtbase 时用 aqt 补装 qtdeclarative，命令见 README。
3. 构建程序为 `build/Still.exe`，本轮实际交付 `dist/Still-refined/Still.exe`，退出旧托盘进程后运行。脚本默认目标仍是 `dist/Still/`；该处旧 exe 本轮无法覆盖，未强行终止用户进程，原目录保留旧包。Qt DLL、qml、plugins、native 与 exe 完整分发，不依赖 Node / Electron。内部应用 target `PixClone` 放在 `src/CMakeLists.txt` 避免扫描 `.tools/`，由 `qt_generate_deploy_qml_app_script` 部署 QML。macOS bundle为Still并携带ICNS，尚未Mac实测。
4. `legacy/electron/` 是原实现归档；旧交接文档见 `HANDOVER-ELECTRON.md`，其中“环境无法执行进程”的结论已经过期，不适用于当前会话。

## 用户已确认

- 支持截图、编辑、贴图、复制、保存，以及贴图上的文本选择。
- 同时考虑 Windows 与 macOS，因此从 Electron 改为 Qt。
- 设置与剪贴板：参考 Apple 的轻盈桌面界面，避免普通工程面板感。侧栏先剪贴板与截图历史，再四类偏好；设置只从侧栏进入，删除剪贴板页头的重复设置按钮。内联表单仅分节画线；剪贴板展示真实内容片段、按需阅读预览和单行常用操作。设置自动应用，无需保存按钮，输入完成后提交，失败恢复原值并保护新输入。截图和编辑工具栏保留原操作与快捷键。
- 品牌为拾影 / Still，英文不用拼音，自绘钴蓝取景框与一页Logo。内部target、QML URI、组织名、数据目录和单实例标识保留PixClone，保证旧历史与设置兼容。
- 通用偏好支持中文 / English与Light / Dark / System，默认zh_CN/system；保存成功后立即更新界面文案及调色板，Widgets标题菜单、画布和工具栏同步，截图图像与用户文本/文件名保持原样。
- 全程离线，不使用 PixPin 的代码、资源或模型。
- 完整剪贴板管理方向：文字、图片、文件及更多格式；默认粘贴到原窗口，提供仅复制；历史本地持久化，可设数量、容量及独立目录。

## 代码职责

| 文件 | 职责 |
| --- | --- |
| `src/app/` | QApplication、单实例、托盘、主窗口入口、热键路由与截图动作交付 |
| `src/settings/` | 合并各入口的设置改动，协调热键预留、设置落盘与已提交状态 |
| `src/capture/` | 冻结帧选区、放大镜、动作工具条、滚动截图会话与预览 |
| `src/clipboard/` | MIME 快照、归档、索引、配额、迁移、预览读取与原窗口粘贴 |
| `src/imaging/` | 图像计算、标注编辑器、贴图、OCR 任务与文字选择 |
| `src/storage/` | 原子保存图片和 JSON、数据目录、截图历史查询、写入和迁移 |
| `src/platform/` | 跨平台热键预留、Win32／Carbon 热键、窗口捕获适配与系统 OCR |
| `src/ui/`、`assets/en.json` | QQuickWidget 桥接、设置草稿、界面本地化与图标 |
| `src/qml/` | 主窗口六页、侧栏、剪贴板、五类设置、共享控件和主题 |
| `assets/still.svg`、`scripts/generate-logo.ps1` | 项目自绘 Logo 及 PNG、ICO、ICNS 导出 |
| `native/Invoke-Ocr.ps1` | 返回 UTF-8 JSON 文本及词级坐标，路径规范化、释放位图和流 |
| `tests/` | 核心、界面、设置与真实系统集成测试 |

## 关键约定

- QScreen / QWidget 几何是逻辑坐标，QImage 与 OCR 是源图片像素；必须按实际 bitmap 尺寸映射，不假设与屏幕缩放系数总是相等。
- OCR 结果不能只保留字符串；`OcrWord` 包含文本、源图位置框、行号。选择以引擎返回的框为单位。
- 编辑或旋转后立即丢弃旧 OCR；异步任务回调校验 generation，不能覆盖新图。
- 贴图、编辑器和截图识字各自只保留最新 OcrJob。关闭或替换请求时销毁旧 job；运行任务的 watcher 独立于窗口存活，确保窗口关闭后仍能归还并发名额。所有调度均在 GUI 线程进行。
- 截图先隐藏当前可见的本应用窗口，取消/完成只恢复原先可见的窗口，不意外显示本来就隐藏的贴图。
- 设置应用先预留新热键，旧键在配置提交期间始终有效。成功后才切换注册和已提交设置；失败只丢弃预留，不释放/重抢旧键。预留键不派发动作，单键冲突仍保留该项旧值；清空组合键是合法动作。
- `QSaveFile` 保护已有文件；保存对话框内部解析默认后缀，确保覆盖确认针对最终路径。
- 滚动拼接无法确认重叠时停止，并保留最后成功结果；不沿用旧算法的猜测性追加。
- `appearance.language` 仅接受zh_CN/en，`appearance.theme`为system/light/dark；默认zh_CN/system，SettingsApply提交成功后才应用。QML使用qsTranslate和QQmlEngine::retranslate()，切换时保留当前页；系统主题变化只在system模式更新调色板。程序生成的图片/文件数量说明仅展示层本地化，存档原文保持不变。
- 深浅外观覆盖QML、QGui/QApplication调色板、编辑器画布与工具栏；图像像素不随主题改变。
- 入库、删除、清空与迁移在 HistoryStore 同一工作队列串行执行。迁移先复制校验，再执行 SettingsApply 提供的配置提交步骤，最后核对并清理旧副本；HistoryStore 不接收整份应用设置。UI 不直接修改历史文件。
- HistoryStore 在独立读队列查询当前目录、加载缩略图和解码原图，读写通过同一文件锁协调。界面关闭或离开历史页后不再交付原图。
- Application 仅拥有滚动截图控制条与交付后的复制/贴图/历史操作；会话状态全部在 ScrollSession。旧拼接尚未完成时不会排入下一次拼接，结束后的旧控制条不得停止新会话。
- Application 析构先销毁主窗口及QML内容，再销毁 ClipboardController、ScrollSession、SettingsApply、HistoryStore、ClipboardArchive，最后让 Hotkeys 成员结束生命周期。控制器以 QPointer 跟踪宿主，后台监听不依赖主窗口存活；MainWindow销毁时取消预览并解除宿主关联。
- 剪贴板归档在专用单线程队列读写。预览和显式操作独立取消，GUI 只保留元数据、小缩略图和当前预览。迁移先发布可读取的目标索引，再提交目录设置，成功后才清理旧副本。格式、文件引用和隐私约定见 `CLIPBOARD.md`。

## 验证与遗留

本轮已用 Qt 6.8.3 / MinGW 13.1 在 Windows 编译并通过八组非integration CTest；真实系统OCR/热键保留既有集成记录，本轮未重跑。最终测试报告在 `build/*-results.txt`，CTest 汇总在 `build/Testing/Temporary/LastTest.log`。

当前QML主窗口默认1000×700、最小800×620，200px分组侧栏、32px工作区左右内边距（窗口宽<900时24px）、24px/500标题。Windows使用Segoe UI并为Han指定Microsoft YaHei UI回退，Mac的Helvetica Neue未实测。导航可见顺序映射后台页号[0,3,1,2,4,5]，方向键同序。页头仅计数、暂停与预览；44px搜索、32px短筛选无贯通基线。内容标题15px/500、片段13px、类型日期11px，单行64px、图片76px、多行／文件80px，间距2px；真实多行文字首个非空行作标题，后续行作片段。阅读栏无灰色外卡与重复标题，仅左分隔与20px内边距，MIME仅日期tooltip。主动作深墨／夜间浅墨反色，底部只保留一次Enter提示。设置有说明行68px、无说明行48px，仅分节18+1+18，无逐行线；输入9px圆角，浅填色，焦点细边。历史列表仍随内容增高至260px，短历史首屏可见操作。

最新证据为 `.impeccable/review/qml-*.png`：23张Windows原生software渲染截图，覆盖中英文、深浅主题、预览、紧凑窗口、暗色按钮状态、独立迁移清理警告和真实临时文件URL的列表／预览。均使用隔离临时合成数据，无用户真实历史，不随产品发布。offscreen截图字体不可用，不作视觉证据；contact表仅作概览。设计规范见 `PRODUCT.md`、`DESIGN.md` 与 `.impeccable/surface-briefs/qml-shell.md`；旧截图和验收记录只保留历史意义。

工具栏渲染证据为 `.impeccable/review/*toolbar-current.png`；局部设计约定见 `.impeccable/surface-briefs/toolbars.md`。界面测试覆盖图标绘制、中文无障碍名称、快捷键、按钮动作和窄窗口的溢出入口。

OCR 队列图片滞留和长截图启动失败首帧残留已修复，含修复前失败、修复后通过的回归测试。说明与探针前后数据见 `MEMORY-FIXES.md`。历史存储优化及长截图分配预算见 `HISTORY-STORAGE.md`；历史架构修复记录见 `ARCHITECTURE-FIXES.md`。当前共九组CTest，本轮Release构建、八组非集成测试通过。QML回归 `build/settings-results.txt` 为21 passed、0 failed，Windows原生 `build/settings-premium-native.txt` 为18 passed、0 failed、3个环境相关skipped；`build/settings-premium-hidpi.txt` 为Qt1.5倍真实鼠标／键盘侧栏与偏好4 passed，303条当前源字符串与305条英文catalog覆盖及占位符检查通过。本轮断言覆盖页头设置按钮移除、侧栏可见顺序键盘导航、真实多行标题／片段、文件路径显示及中英字体一致性。`premium-finish-review.md` 的ship仅覆盖本轮已评分项、文件路径场景及直接可见回归，共23张最终截图；`premium-direction-review.md`为独立结构诊断。

历史真实Windows粘贴证据为 `build/qml-native-paste-results.txt`：3 passed、0 failed、0 skipped，保留一次 `OleSetClipboard 0x800401d0` 短时占用QWARN；测试恢复备份最多重试10次并检查恢复结果，持续失败会QFAIL。本轮界面改动后未重放真实系统粘贴。

`build/premium-smoke-results.json` 记录本轮部署版 `dist/Still-refined/Still.exe` 在清空SDK PATH/QML/plugin搜索环境、独立 `.tools/smoke-premium` 配置、不监听剪贴板、Windows默认渲染后端（未强制software）的四组合启动：zh_CN-light、zh_CN-dark、en-light、en-dark均QML Ready、退出0。`build/Still.exe`与当前部署exe的SHA256相同：`92DBB4E7A36630716611FCD84184E50858E3E93D5299B8C43E60A8FB7AC3166D`。原 `dist/Still/` 因旧exe无法覆盖保留旧包；不得用旧smoke或旧hash代表本轮交付。这些是启动检查，不等同于GPU性能或完整硬件图形验收。

尚需 Mac 编译/运行、屏幕阅读器、多屏混合 DPI 实机验收、HDR、完整滚动页面、长期内存/延迟与GPU性能测试；系统粘贴仍受权限和剪贴板占用环境影响。未迁移的非核心设置和功能列在 README 的当前限制中，不要把保留字段当作已实现。
