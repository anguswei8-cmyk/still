# Still 后续迭代计划

更新日期：2026-11-07。范围：Still 的 Windows/macOS 桌面产品能力与后续实现路线。

本文按当前源代码记录已有能力和待办，保留 P0–P3 路线与验收条件。高级识别、翻译、图像修复、录制增强和配置同步作为后续方向保留，不以账号、会员、订阅或付费解锁区分功能。

## 当前目标：只完成 P1

按用户最新要求，本次目标仅推进 P1（ST-101 至 ST-110），以已有实现为基线逐项补齐最小交付、相关回归和验收记录。ST-109 的Mac增量已验证，Windows源码已接入但未在原生环境编译或运行。用户确认目前只有这台Mac，Windows原生项目保留待验收。ST-110 已按用户选择接入 OCR 文字校对首增量，本地模拟服务和 Mac 后台钥匙串专项通过；6组相关回归已通过，真实远端及剩余原生验收未完成，本项保持部分完成。

P0 只处理 P1 所必需的依赖、缺陷和回归，不再单独扩项；P2、P3 仅保留在路线图中，不属于当前目标，不自动进入开发。完成 P1 的交付和验收后结束本次目标，不等待 P2、P3。环境受限的验收明确标记待验收，不能把编译、离屏通过或跳过当作原生验收完成。继续保护现有截图、贴图、OCR、剪贴板、存储行为、用户数据和工作区已有改动。

## 计算方式与使用原则

- **本机优先**：系统接口、本地算法和本地模型优先；截图、剪贴板、标注、存储等基础流程无需网络或 API Key。
- **用户自备 API Key**：需要云端模型能力时，用户可配置服务地址、模型和自己的密钥，直接使用所选服务。服务商调用费用由用户自己的账号承担，Still 不加会员门槛。
- **逐项交付**：每项能力先交付一条经过验证的可用路线，再补另一条；不要求一次实现全部本地和云端后端，也不预先搭建通用模型平台。
- **明确执行位置**：界面区分本机与 API 任务；本地能力不足时给出说明和可选方式，不静默上传或自动切换服务。API 请求只发送用户此次选定的文字或图像区域。
- **密钥和数据**：密钥使用系统凭据存储，可替换/删除，不写入日志、普通配置备份或同步文件。联网处理、模型下载和资源占用有明确反馈；任务可取消，失败保留原内容。
- **能力与硬件匹配**：本地模型交付前验证 CPU/GPU、内存、速度和精度；基础功能不强制下载大型模型，不把暂不支持的硬件配置显示成已可用。

下表是后续实现方向；当前 ST-110 仅接入文字校对 API 首增量，不表示其他 API 能力或本地高级模型已接通。

| 能力 | 本机路线 | 自备 API Key 路线 |
| --- | --- | --- |
| OCR、多语言识别 | 系统 OCR，逐步增加本地识别模型 | 当前提供文字校对；视觉／多语言识别服务后续逐项接入 |
| 表格、公式、文档识别 | 本地视觉模型与结构化处理 | 用户配置视觉模型，结果可校对并在本机导出 |
| 文字/图片翻译 | 本地翻译模型，图像排版在本机处理 | 用户配置翻译或语言模型服务 |
| 隐私遮挡、智能擦除 | 本地检测、遮挡与图像修复模型 | 可选用户指定图像处理服务，保留原图和可撤销结果 |
| 录屏、音频、键鼠演示、画中画和剪辑 | 系统捕获、本机 CPU/GPU 编码与编辑 | 不作为联网依赖，无需 API Key |
| 语音朗读与音频导出 | 系统语音或本地模型 | 可选用户配置的语音服务 |
| 配置同步 | 用户选择的同步文件夹 | 可选用户自有存储服务；凭据不随配置同步 |

## Still 当前代码依据

| 依据 | 入口与职责 |
| --- | --- |
| C01 | [application.cpp](../src/app/application.cpp)、[capture_delay.cpp](../src/capture/capture_delay.cpp)、[capture_presets.cpp](../src/capture/capture_presets.cpp)：托盘、八类全局快捷动作、可取消延时、当前运行中复用上次区域、命名区域预设与截图流程、图片/文字/HTML 贴图与历史交付 |
| C02 | [overlay.cpp](../src/capture/overlay.cpp)、[image.cpp](../src/imaging/image.cpp)、[platform_mac.mm](../src/platform/platform_mac.mm)、[platform_win.cpp](../src/platform/platform_win.cpp)：选区、像素宽高与固定比例、窗口吸附、键盘微调、放大镜取色、平台截图权限 |
| C03 | [image.h](../src/imaging/image.h)、[image.cpp](../src/imaging/image.cpp)、[editor.cpp](../src/imaging/editor.cpp)：11 类标注、有序对象与变更记录、形状命中、选择/移动/样式/改字/删除、裁切/翻转/灰度/反色/亮度、撤销重做及 PNG/JPEG 输出；对象只保留在当前编辑文档 |
| C04 | [pin.cpp](../src/imaging/pin.cpp)、[text_pin.cpp](../src/imaging/text_pin.cpp)、[html_document.cpp](../src/imaging/html_document.cpp)、[pin_window.cpp](../src/imaging/pin_window.cpp)、[pin_state.h](../src/imaging/pin_state.h)：图片/OCR、纯文本和安全 HTML 文字排版，共享窗口布局、关闭和恢复；图片保留缩放/旋转/编辑/关闭确认 |
| C05 | [scroll_session.h](../src/capture/scroll_session.h)、[scroll_session.cpp](../src/capture/scroll_session.cpp)、[scroll_capture_ui.cpp](../src/capture/scroll_capture_ui.cpp)：手动纵向拼接、预览、限额及结果交付 |
| C06 | [ocr.cpp](../src/imaging/ocr.cpp)、[ocr_result_dialog.cpp](../src/imaging/ocr_result_dialog.cpp)、[platform_mac.mm](../src/platform/platform_mac.mm)、[platform_win.cpp](../src/platform/platform_win.cpp)：后台 OCR 队列、Apple Vision / Windows 系统 OCR、独立原文／校对稿、显式换行／空格整理与纯文本复制／拖出 |
| C07 | [main_window.cpp](../src/ui/main_window.cpp)、[settings_apply.cpp](../src/settings/settings_apply.cpp)、[settings_backup.cpp](../src/settings/settings_backup.cpp)、[settings_backup_dialog.cpp](../src/settings/settings_backup_dialog.cpp)、[SettingsPage.qml](../src/qml/SettingsPage.qml)：可见设置、校验和保存、版本化备份与预览／严格导入／全部默认，中文/英文与浅深主题 |
| C08 | [clipboard_controller.cpp](../src/clipboard/clipboard_controller.cpp)、[clipboard_archive.cpp](../src/clipboard/clipboard_archive.cpp)、[image_copy.cpp](../src/clipboard/image_copy.cpp)、[history.cpp](../src/storage/history.cpp)：剪贴板历史、搜索/固定/重放、图片/临时文件复制、配额、目录迁移与截图历史 |
| C09 | [store.cpp](../src/storage/store.cpp)、[image_export.cpp](../src/storage/image_export.cpp)、[pin_session.cpp](../src/storage/pin_session.cpp)、[region_presets.cpp](../src/storage/region_presets.cpp)：数据目录兼容、原子保存、文件名模板、无覆盖快速保存、独立贴图会话和版本化命名区域预设；保留的旧配置字段不等于已接通功能 |
| C10 | [settings_test.cpp](../tests/settings_test.cpp)、[settings_backup_test.cpp](../tests/settings_backup_test.cpp)、[settings_backup_dialog_test.cpp](../tests/settings_backup_dialog_test.cpp)、[ui_test.cpp](../tests/ui_test.cpp)、[ocr_result_dialog_test.cpp](../tests/ocr_result_dialog_test.cpp)、[pin_session_test.cpp](../tests/pin_session_test.cpp)、[region_presets_test.cpp](../tests/region_presets_test.cpp)、[capture_presets_test.cpp](../tests/capture_presets_test.cpp)、[native_test.cpp](../tests/native_test.cpp)：界面、配置备份／导入、OCR校对、贴图会话、区域预设回归与原生功能验证入口 |
| C11 | [startup.h](../src/platform/startup.h)、[startup_mac.mm](../src/platform/startup_mac.mm)、[startup_win.cpp](../src/platform/startup_win.cpp)、[startup_task.cpp](../src/platform/startup_task.cpp)、[StartupRow.qml](../src/qml/StartupRow.qml)、[startup_native_test.cpp](../tests/startup_native_test.cpp)、[startup_task_test.cpp](../tests/startup_task_test.cpp)、[startup_windows_test.cpp](../tests/startup_windows_test.cpp)：本机登录启动、平台管理入口及隔离后台测试；Mac已验证登记范围，Windows源码接入但原生编译／运行待验收 |
| C12 | [api_proofreader.cpp](../src/imaging/api_proofreader.cpp)、[api_proofread_dialog.cpp](../src/imaging/api_proofread_dialog.cpp)、[api_profile.cpp](../src/settings/api_profile.cpp)、[api_settings_dialog.cpp](../src/settings/api_settings_dialog.cpp)、[api_credentials.h](../src/platform/api_credentials.h)、[api_credentials_mac.mm](../src/platform/api_credentials_mac.mm)、[api_credentials_win.cpp](../src/platform/api_credentials_win.cpp)：显式文字快照校对、预览／应用、独立非密钥 profile 和按地址隔离的系统凭据；回归入口为 `tests/api_*` 与 C10 的 OCR 结果用例 |

## 能力目标与当前差距

“已有”表示当前源代码有实际入口和调用；“部分”表示基础能力存在但范围有差距。除单独说明外，Still 的 Mac 原生功能仍需实机验收。

| 编号 | 能力目标 | Still 现状 | 后续任务 |
| --- | --- | --- | --- |
| F01 | 区域/窗口范围/整屏截图，复制、保存、贴图 | 已有基本流程，C01/C02 | ST-001 验收 |
| F02 | 子 UI 元素吸附、父子层级切换 | 部分：只有窗口矩形吸附，没有按钮/文本框等子元素检测，C02 | ST-201 |
| F03 | 延时截图、固定区域和历史选区/预设复用 | 部分：已有 1–60 秒可取消延时、当前运行中重复上次区域及命名区域预设跨重启复用；未提供自动最近选区列表，跨平台实机验收待补，C01/C02/C09 | ST-106 |
| F04 | 数字宽高、固定比例、异形/圆角截图 | 部分：已有最终图像像素宽高与五种固定比例，和矩形选区、八手柄及键盘同步；异形/圆角截图待实现，标注中的圆角矩形不是圆角截图，C02/C03 | ST-106、ST-201 |
| F05 | 截图光标、可配置辅助线和放大镜 | 部分：有放大镜和尺寸信息，没有光标合成及完整辅助线配置，C02 | ST-202 |
| F06 | 跨屏选区与跨屏长截图 | 部分：每屏分别采集，一次框选限于单屏，C01/C02 | ST-207 |
| F07 | 多格式取色、复制坐标 | 部分：可复制像素 Hex 色；没有 RGB/HSV/HSL 格式切换及坐标复制入口，C02 | ST-202 |
| F08 | 基础几何、画笔、荧光笔、文字、序号、马赛克 | 已有 11 类工具和撤销重做，C03 | 维护已有能力 |
| F09 | 已有标注对象再选中、移动、改字、改样式、删除 | 当前编辑文档已支持选择、移动、改字/样式、删除与撤销重做；PNG/历史/贴图重开及重启后仍扁平化，C03/C04 | ST-104 |
| F10 | 折线、模糊、橡皮擦、聚光灯、水印、放大镜标注 | 缺少；取色放大镜不等于放大镜标注，C03 | ST-203 |
| F11 | 自动隐私马赛克/智能擦除 | 缺少；已有手动马赛克不等于自动隐私识别，C03 | ST-304 |
| F12 | 本地 OCR 与图片文字拖选复制 | 已有系统 OCR 和位置框选择；Mac 交互仍待验收，C04/C06 | ST-001 |
| F13 | OCR 结果校对、换行/空格/标点处理、文字拖出 | 部分：已有原文／校对结果、显式本机整理、撤销和纯文本复制／原生拖出；另接入显式 API 文字校对首增量，真实远端及跨平台验收待补，C04/C06/C12 | ST-107、ST-110 |
| F14 | 二维码识别 | 缺少，C02/C04/C06 | ST-206 |
| F15 | 表格识别及结构化导出 | 缺少；纯 OCR 文本不能代替单元格结构，C06 | ST-302 |
| F16 | 公式识别与 LaTeX 输出 | 缺少，C06 | ST-303 |
| F17 | 文档版面识别和 PDF/Markdown/Word 等导出 | 缺少版面结构识别和对应导出流程，C06 | ST-305 |
| F18 | 图片/文本翻译 | 缺少；计划支持本地模型或用户自备 API Key，C06 | ST-306 |
| F19 | 图片贴图缩放、透明度、置顶、旋转、隐藏 | 已有主要操作，旋转仅 90°；不应重复开发，C04 | ST-001 验收 |
| F20 | 文本/HTML、文件、颜色、LaTeX 内容贴图 | 部分：图片、纯文本及 HTML 文字排版已实现；HTML 不加载图片/外部资源，RTF-only 仍取纯文本；文件/颜色/LaTeX 贴图待实现，C01/C04/C08 | ST-103、ST-208、ST-303 |
| F21 | 启动恢复未关闭贴图、恢复关闭项、贴图组 | 部分：已有当前运行的关闭恢复、正常退出会话及启动/手动恢复图片和布局；贴图组未实现，Windows/实际多屏待验收，C01/C04/C09 | ST-102、ST-204 |
| F22 | 锁定、鼠标穿透、缩略图、贴图多选 | 缺少，C04 | ST-204 |
| F23 | 裁切、翻转、灰度、反色、亮度等图像处理 | 已有列出的基础处理能力，可撤销并保留当前标注可编辑；亮度支持实时预览，Windows 原生待验收，C03/C04 | ST-105 |
| F24 | 内容拖出、打印 | 缺少；已有复制和保存，C03/C04 | ST-208 |
| F25 | 实时窗口贴图、多个窗口贴图 | 缺少；静态窗口截图不等于窗口持续刷新，C01/C04 | ST-307 |
| F26 | 手动纵向长截图和结果预览 | 已有 Beta；失配等待、内存上限和输出动作已实现，C05 | ST-001 验收 |
| F27 | 横向/自动滚动、暂停调整、裁剪和超长输出 | 缺少相应入口；当前只手动纵向，默认最大高度 24000、80 个有效帧及 256 MiB 图像预算，C05 | ST-205；超长输出先评估 |
| F28 | MP4/GIF/WebP 录屏、暂停、音频和录制增强 | 缺少；项目未引入录制流程，C01/C02/C05 | ST-301 |
| F29 | 快速保存、文件名变量、重名策略、格式/质量选项 | 部分：已接通快速保存、模板预览、数字后缀、PNG 压缩/JPEG 质量、默认目录、保存后复制及复制临时图像文件；Mac Finder及退出后粘贴通过，其他编码取决于writer，Windows待验收，C03/C07/C08/C09 | ST-101 |
| F30 | 配置导入导出、单项/全部恢复默认 | 部分：已有版本化备份、严格导入、全部默认和当前表单单项默认；完整跨平台原生验收待补，C07/C09 | ST-108 |
| F31 | 开机启动 | 部分：Mac登记／查询／撤销及同卷文件系统移动已验证；Windows Task Scheduler源码与公共XML测试已接入，Windows编译／系统登记、重新登录及Finder／跨卷移动仍待验收，C07/C11 | ST-109 |
| F32 | 自定义动作、应用忽略列表、局部快捷键、鼠标映射 | 部分：八类全局动作可改键并处理冲突，局部键和鼠标行为主要固定，C01/C04/C07 | ST-209 |
| F33 | 托盘点击动作、悬浮工具栏、按钮排列、主题细调 | 部分：有托盘菜单、固定工具栏、中英和系统/浅深主题，没有完整定制，C01/C03/C07 | ST-209；外观细调按需求追加 |
| F34 | 持久剪贴板历史、搜索/固定、目录迁移和配额 | 已有独立实现，C08；作为核心能力持续维护 | 继续维护 |
| F35 | 跨设备配置同步 | 缺少；计划使用用户自选同步目录或自有服务 | ST-308 |
| F36 | 多语言 OCR、可选模型与语音朗读/音频导出 | 部分：已有单项语言设置和系统 OCR，没有完整语言/模型选择及语音流程，C06/C07 | ST-210 |
| F37 | 编码预览、输出大小估计和 PDF 分页 | 部分：已有 PNG/JPEG 保存，没有编码预览、分页与页边距控制，C03 | ST-211 |

长截图需要针对固定区域、动画、重复内容和不同应用分别验证，不以“任意页面无损拼接”作为验收承诺。

## 分阶段待办

按 P0 → P1 → P2 → P3 排序。同阶段内按下表顺序推进；大功能逐项确认实现成本后再安排日期。

### P0：先建立可交付的 Mac 基线

| 任务 | 范围与责任位置 | 完成标准 |
| --- | --- | --- |
| ST-001 | Mac 原生验收；`platform`、`capture`、`clipboard`、`imaging` | 首次/拒绝/重新授权截图权限可恢复；辅助功能未授权时仍能手动粘贴；热键不抢占冲突键；中文 OCR、贴图选字、多屏/混合 DPI、长截图和保存各留实机记录。Still 新 bundle 标识下重新核对系统权限。 |
| ST-002 | UI 回归修复，离屏已完成、原生待复验；`tests/ui_test.cpp` | 选字测试等待 OCR 实际完成，取消关闭提示后保持贴图可见，弹层销毁后仍能正常失焦隐藏；原有断言保留。原生交互随 ST-001 复验，不以离屏通过代替实机验收。 |

2026-11-05 已重跑 Mac 构建与全部非 integration CTest，10/10 组通过。新增回归覆盖普通编辑器不提供无接收目标的“应用并关闭”、已有贴图应用编辑结果、退出时完成排队设置与草稿，以及迁移失败后的后续设置提交。日志在本机构建目录 `ui-results.txt`、`settings-results.txt`、`settings-apply-results.txt`，不视为 Windows 或 Mac 原生完整验收。

系统 OCR 已在沙箱外用合成图片通过英文、默认中文及位置框、空白图片和失败反馈四种场景（6 passed / 0 failed，含初始化与清理），记录在 `cmake-build-debug/ocr-results.txt`。此前受限沙箱内的 Vision 调用返回失败却未提供 NSError，现会明确报错；同时修复默认 `zh-Hans-CN` 与 Vision `zh-Hans` 的匹配，以及中文模型逐字返回英文/数字位置框时的错误空格。该验证不包含屏幕录制授权或真实截图交互。

2026-11-06 P0 补验：Mac 原生主窗口最小化/恢复、Dock 显隐通过（`p0-mac-window-results.txt`，4 passed，含初始化/清理）；真实系统热键冲突保留旧注册、分阶段替换通过（`p0-hotkeys-results.txt`，4 passed）。截图首次/拒绝/重授权限、真实跨应用粘贴、多屏/混合 DPI 与完整长截图仍待实机记录，ST-001/ST-002 不标记全部完成。

2026-11-06 P0 贴图失活补验：新增 `settings_test.cpp::pinsStayVisibleWhenTheApplicationIsInactive`，注册为独立串行 integration `still_mac_pin_activation`。图片、纯文本与 HTML 三类贴图分别覆盖新建置顶、关闭再开启置顶和恢复非置顶布局，共9个窗口；先确认本应用激活及每个原生窗口可见，再协作切换到自有合成测试进程，确认目标激活、本应用失活后 Qt 和原生窗口仍可见。独立审查补强了等待100ms后、采样前再次确认激活状态的断言，避免期间切回导致假阳性，复验通过。现有生产代码通过，**未复现贴图自动隐藏的缺陷，未修改 PinWindow 的窗口策略**。最终原生 **3 passed / 0 skipped**（`cmake-build-debug/mac-pin-activation-results.txt`，2.085秒），CTest **1/1 通过**（`pin-inactive-final-ctest-results.txt`，3.09秒）；只证明这些样本在失活后未隐式隐藏，不证明窗口叠放、无遮挡像素或完整辅助功能。

测试目标最终重编通过（`pin-inactive-final-build.txt`）；离屏仅验证平台前置条件，**2 passed / 1 skipped**（`pin-inactive-offscreen-results.txt`），不计为原生通过。初期沙箱无法连接窗口服务，日志仅含初始化标题；随后两次原生执行停在应用未激活的前置条件，当时只读系统状态确认再次锁屏，分别保留 `pin-inactive-sandbox-results.txt`、`pin-inactive-activation-precondition-results.txt`、`pin-inactive-locked-results.txt`，均不作为贴图缺陷证据。当前测试复用普通“打开剪贴板”入口建立前台条件，等待15秒后仍无法激活则明确跳过，满足前置条件后的实际切换与可见性断言保留。最终自身截图为 `.impeccable/review/qml-pin-inactive-{image,plain,html}.png`，仅合成内容。此轮未改生产行为，不重复无关全量测试；P0 其余待验收项及 ST-106 剩余增量保持。

### P1：优先补齐日常操作

2026-11-06：ST-101 首个增量已实现：Ctrl/Cmd+Shift+S 快速保存，Ctrl/Cmd+S 保留对话框；选区、编辑器、贴图和长截图均接入。模板支持 `{date}`、`{time}`、`{ms}` 并预览，重名按数字后缀处理；先独占预留目标名，再用 QSaveFile 编码并提交，失败移除本次预留。PNG 压缩保持无损、JPEG 质量可调；设置更新现有窗口，保存失败保留内容。此项仍为**部分完成**，临时文件复制的后续增量见本节末尾。本机 Qt 6.12 枚举未提供 WebP/TIFF writer，未显示为可用格式；待有相应插件的构建环境验证后再接入，不新增无效选项。

本增量在 Mac Qt 6.12 构建通过，非 integration CTest **11/11 组通过**；导出 9 passed、UI 35 passed、设置 40 passed / 12 环境 skipped。覆盖命名校验、已有文件及悬空链接保护、4 轮各 12 个不同图像的并发保存、PNG 无损/JPEG 质量、失败清理、快捷键与正常保存对话框、已有窗口设置更新。新增保存设置原生交互及中英/深浅/1000×740 与 800×620 布局 **7 passed**（`save-native-settings-results.txt`）；对应截图为 `.impeccable/review/qml-save-*.png`，仅合成数据与窗口自身截图。独立界面审阅 `ship` 仅覆盖此新增设置区域与所提供工具栏截图，不代表 Windows 或整个产品原生验收。测试日志均在 `cmake-build-debug`。

| 任务 | 对应差距与最小交付 | 完成标准 |
| --- | --- | --- |
| ST-101 | F29，快速保存和命名；`storage/store`、`imaging/editor`、设置页 | 一次操作保存到配置目录；模板预览、重名数字后缀、PNG/JPEG 质量参数有效；已有文件不因失败被破坏。WebP、TIFF 等格式确认实际编码支持后逐项增加；再支持按设置将图像复制为临时文件。 |
| ST-102 | F21，关闭恢复和正常退出会话恢复；已实现并通过 Mac 回归，Windows/实际多屏待验收 | 恢复最近关闭贴图；重启后恢复图片、位置、尺寸、透明度和置顶状态；缺失图片可说明原因；不把截图历史配额直接当贴图会话生命周期。 |
| ST-103 | F20，纯文本与安全 HTML 文字排版已实现，Mac 已验证，Windows 原生待验收 | 剪贴板文字可直接贴出、选择和复制；长文本可滚动；明确与图像 OCR 的区别。HTML 只读展示不加载远程资源或执行内容。 |
| ST-104 | F09，当前文档对象编辑已实现，Mac F2／弹窗焦点已补验；Windows 原生待验收 | 能选择、移动、改字/样式和删除对象，撤销重做覆盖这些操作。首轮仅保证当前编辑文档；若要求关闭或重启后继续编辑，再加入独立保存的标注数据，不能声称从普通 PNG 恢复对象。 |
| ST-105 | F23，裁切、水平/垂直翻转、灰度/反色和亮度已实现，Windows 原生待验收 | 复制/保存按处理后结果输出，操作可撤销，当前标注仍可编辑；亮度预览取消或零值不改原状态，确认一次可撤销；图像变化后 OCR 框失效并重新识别；不改动原始文件。 |
| ST-106 | F03/F04，可取消延时、当前运行中重复上次区域、像素宽高、固定比例及命名区域预设已实现；完整跨平台实机验收待补 | 倒计时可取消，不提前冻结屏幕；区域按实际屏幕/DPI 映射；外接屏拔除后提示重新选择；数字与鼠标调整同步。先矩形，异形区域留到 P2。 |
| ST-107 | F13，原文／校对结果、显式换行／空格整理及复制已实现并通过 Mac 回归；TextEdit复制拖出及接收端撤销／重做通过，Windows待验收 | 可查看/修改结果，选择保留/移除换行、空格处理；保留原始结果，不默认破坏代码或表格排版；复制和文本拖出可用。 |
| ST-108 | F30，本地配置备份、预览／严格导入、全部默认与当前表单单项默认已实现；Mac系统文件／确认面板通过，Windows待验收 | 文件带版本；导入前校验并保留现有配置，失败不部分写入；目录、快捷键冲突明确反馈；不把机器绝对路径盲目应用到另一台设备。 |
| ST-109 | F31，Mac登记及同卷文件系统移动增量已验证；Windows源码已接入但原生编译、系统登记及实际登录／Finder或跨卷移动仍待验收 | 开关与真实系统登记一致；关闭或应用移动后可处理失效登记；与平台授权、打包结果一起验收，不只保存一个布尔字段。 |
| ST-110 | 部分：首个实际 API 功能为 OCR 文字校对；地址／模型／系统密钥、连接测试、显式发送及预览应用已接入，相关回归通过，真实远端与剩余原生验收待补，C12 | 本地处理不要求密钥；显式选择选区或整稿及目的服务，支持连通性检查、取消、错误反馈和真实用量；先预览再应用、原文和撤销保留。密钥进入系统凭据存储，不进入日志或普通配置导出；不得静默从本地切到云端。 |

2026-11-06 ST-101 第二个增量：新增默认关闭的 `save.copyAsFile`，实际接入截图/长截图结果、编辑器、贴图和保存后复制，按现有模板与 PNG/JPEG 编码参数生成文件 URL。选中的 OCR 文字仍为文字，剪贴板历史仍重放原格式。编码完成前不替换剪贴板；临时文件不随函数返回、剪贴板更换或窗口关闭删除，应用退出也不主动删除。当前使用系统临时目录且不另设配额，系统清理后文件历史引用可能失效；长期保留应正常保存。

本增量 Mac 构建与非 integration CTest **11/11 组通过**：导出 **12 passed**，UI **37 passed**，设置 **40 passed / 12 环境 skipped**；默认图片格式、编码后文件内容、原剪贴板失败保护、文件历史序列化、标注输出、窗口关闭后文件留存、OCR 文字选择、已有窗口设置更新及保存后复制已覆盖。原生设置开关/持久化及中英深浅、正常/紧凑布局 **7 passed**（`copy-file-native-settings-results.txt`），只用隔离数据和窗口自身截图。真实 Finder/Explorer 文件粘贴、退出后跨应用粘贴和 Windows 原生表现尚待验收，不将 MIME/离屏测试等同于系统文件粘贴。ST-101 保持部分完成；WebP/TIFF 等格式仍受实际 writer 缺失限制，下一项独立实现为 ST-102。

独立界面审阅 `ship` 仅覆盖新增“复制为图像文件”设置行在上述四张截图中的呈现；无新增视觉系统或 material fixes。记录在 `.impeccable/review/file-copy-design-review.md`，不扩大为键盘焦点、跨应用粘贴或全产品通过。

2026-11-06 ST-102：已接通当前运行的关闭恢复与正常退出会话恢复。托盘“贴图操作”提供两个恢复入口，贴图右键“更多操作”可恢复关闭项；启动恢复默认关闭，可在通用偏好开启。关闭栈最多20张/256 MiB、至少保留最近一张；会话独立于截图历史，PNG先完整写入后原子提交索引。Qt Quit 事件先捕获未关闭窗口，再允许 Qt 关闭窗口；真实退出子进程验证了不会误存空会话。后台读取按当前剩余容量执行，重复恢复不重开已有窗口；缺图、索引损坏和上限导致恢复不全时保留原会话。旧屏幕缺失时窗口移到当前可用区域。关闭栈不跨运行，不保证崩溃/断电恢复；Windows 和真实多屏变更仍待验收。下一项独立实现为 ST-103。

同轮按用户新要求精简贴图和菜单栏右键：贴图5个主入口、托盘6个主入口；低频动作移入分组，保留全部功能和快捷键。这是 ST-209 的菜单组织增量，不表示自定义按钮排列/快捷键映射已完成。

最终验证：Mac Qt 6.12 构建、非 integration CTest **12/12 组通过**；贴图会话存储 **10 passed**、UI **40 passed**、设置 **57 passed / 12 环境 skipped**。原生关闭/重启恢复、真实 Qt Quit 子进程和双屏两个方向各15次布局恢复 **6 passed**（`pin-layout-final-results.txt`）。修复了原生首次显示回调覆盖恢复位置的问题，保留修复前失败日志 `pin-screen-before.txt` 和修复后结果；早期 `compact-menu-native-results.txt` 中的双屏失败不能当最终通过记录。菜单/设置的12个原生显示场景已通过，24张自身截图覆盖中英深浅、正常/紧凑设置、两类主菜单及其单层分组；独立审阅 `ship` 与文档记录在 `.impeccable/review/compact-menu-design-review.md`，只证明该界面的呈现与组织。屏幕移除的离屏约束也有回归，但不据此宣称热插拔、混合 DPI 截图或 Windows 实机已通过。

2026-11-06 ST-103 首个增量：新增独立只读纯文本贴图，直接剪贴板与历史 text/rich/link 入口复用现有贴图动作；HTML 仅取纯文本。可选字、全选、复制、滚动长文、拖标题移动与右下角调整大小；双击选词，文字不走 OCR。复制按源文本 UTF-16 范围截取，保留 CRLF、制表、首尾空白、emoji、Unicode 分隔符及 NUL；单项上限 32 MiB UTF-8。图片继续优先，文件 URL 不误贴成路径。文字菜单仅复制、全选、更多、关闭，不新增托盘或历史主菜单行。

`PinWindow` 共享身份、布局和关闭生命周期，图片 OCR/图像操作留在 Pin，TextPin 只管文字。独立会话升级 v2，图片/UTF-8 文字分别写 PNG/TXT，兼容读取 v1 图片会话；原子提交、旧会话保护、关闭栈容量和启动默认关闭不变。安全富文本展示尚待实现，ST-103 保持部分完成。

本增量 Mac Qt 6.12 全量构建、非 integration CTest **12/12 组通过**：UI **43 passed**、设置 **63 passed / 12 环境 skipped**、贴图会话 **15 passed**、剪贴板派发 **16 passed**。覆盖原样复制与 Unicode 位置、字节上限、旧版迁移、混合关闭/重启恢复、直接剪贴板的图片优先和文件 URL 排除。原生混合会话、真实 Qt Quit、双屏恢复与文字窗口截图 **10 passed**（`text-pin-native-results.txt`）；修复深色关闭按钮和滚动条后，12张中英深浅/正常紧凑/滚动与菜单截图重新捕获，显示回归 **6 passed**（`text-pin-native-visual-results.txt`）。这些原生场景只使用隔离数据和合成内容，不代表 Windows、全部焦点/辅助功能或崩溃恢复已验收。

独立界面复核将两项深色修正均评为 `resolved`，最终 `ship` 仅覆盖这两项；记录见 `.impeccable/review/text-pin-design-review.md`。沿用既有视觉系统，未借本轮修改 DESIGN 或修复已有平台元数据漂移。

2026-11-06 ST-103 第二增量：直接剪贴板与历史 `text/html` 自动进入只读富文本贴图，使用 Qt HTML 文字排版子集保留标题、粗斜体、列表、表格、预格式文本和内联 CSS 文字格式；原纯文本仍精确复制源范围。富文本复制选区或全文的可见纯文本，不携带 HTML、不误用原始后备文本；原始剪贴板与历史 MIME 保持。仍为4个主菜单动作，不新增托盘/历史根菜单行。

`SafeHtmlDocument` 在解析前替换 Qt 图片处理器，资源返回有效空值以阻断实例/全局提供方，解析后移除图片和链接动作，并清理字符与段落字符格式的 tooltip。纯文本后备提取也使用同一实现。回归覆盖本地、相对、qrc、data、远程图片、CSS import/background、脚本/链接和富文本 tooltip；首空段 tooltip 真实复现，保留 `html-pin-tooltip-before.txt`（失败）与 `html-pin-tooltip-after.txt`（3 passed）。不运行浏览器或 JavaScript，不加载图片/外部资源；RTF-only 仍为纯文本，不保证完整网页保真，也不声称进程隔离或无资源耗尽风险。

会话统一写 v3，原文后备 TXT 与 HTML 分别完整写入后原子提交，兼容读取 v1/v2；合计上限32 MiB UTF-8，缺失/损坏任一内容文件跳过整张，恢复不完整时保留旧会话。旧版拒绝 v3，避免静默丢格式。关闭、会话恢复及真实 Qt Quit 已覆盖图片/纯文本/HTML 三种内容。

本增量全量构建及非 integration CTest **12/12 组通过**（`cmake-build-debug/html-pin-ctest-results.txt`）：UI **46 passed**、设置 **67 passed / 12 环境 skipped**、会话 **19 passed**、剪贴板归档 **26 passed**、派发 **17 passed**。原生历史入口、关闭/重启恢复、真实退出和12张中英深浅/正常紧凑/选择/滚动/菜单截图 **8 passed**（`html-pin-native-results.txt`），只用隔离合成数据和窗口自身截图。Windows、全部辅助功能/焦点路径仍待验收；保留总目标，下一项独立开发为 ST-104。

独立界面审阅 `ship`，无 material fixes，范围仅为本轮12张 macOS 原生界面截图；资源隔离依据单独的源码与功能回归，不由视觉结论代替。记录见 `.impeccable/review/html-pin-design-review.md`；保留既有视觉系统与平台元数据漂移。

2026-11-06 ST-104：新增 V“选择标注”，按源图形轮廓及缩放后的逻辑像素容差从顶层命中，拖动整体移动，方向键按源像素微移、Shift 加速。选中对象复用颜色/线宽控件，文字与序号可 F2/双击修改，取消保留，清空则删除标签。画布 Delete/Backspace 删除选中对象；选择模式无选中时不误删，绘制模式保留原有撤销末项行为，在线宽输入框中 Delete 只编辑数字。Esc 先取消当前手势/选择，再次关闭编辑器；换工具取消未提交预览。不新增右键菜单项，保留11个绘制工具与既有快捷键。

ImageDocument 使用当前对象列表和单次新增/替换/删除的 before/after 变更记录，撤销重做恢复层序与内容，实际新修改截断 redo，无变化保留 redo。删除序号后自动补当前未用的最小正整数。移动预览从原图按层序重绘，松手只提交一次，避免旧形状残留或马赛克二次采样。复制/保存/贴图/应用/OCR 输出已提交图像，不含选择框或未完成预览。没有加入对象持久化：PNG/JPEG、历史、贴图会话仍为扁平图片，重开或重启不恢复旧标注对象。

线宽数字框修复了 Qt QSS 与 NoButtons 组合将编辑区压成1px的问题，删除该覆盖并恢复默认步进按钮；保留修复前失败 `annotation-width-before.txt` 与修复后 **3 passed** 的 `annotation-width-after.txt`。新增宽度可容纳当前文字的回归，不能只断言 SpinBox 数值。

本增量全量构建和最终非 integration CTest **12/12 组通过**（`cmake-build-debug/annotation-ctest-final-results.txt`，19.73秒）：核心 **37 passed**、UI **55 passed**、设置 **73 passed / 12 环境 skipped**。覆盖形状/层序、缩放拖动和单步撤销、文字/序号接受取消与清空、输入焦点、手势取消、样式和输出无选择框。最终原生合成对象操作与8张中英深浅/正常紧凑窗口及工具栏截图 **7 passed / 1 skipped**（`annotation-native-final-results.txt`），只用隔离数据与控件自身截图，不读取桌面或真实剪贴板。

原生 F2/文字弹窗后焦点首轮因锁屏未验收：初始失败发生在弹窗前，记录显示测试应用未激活；CUA 原生入口明确报告 Mac 已锁屏且无法自动解锁。用例在原生应用不处于前台时明确跳过，前台条件满足后的修改/取消/焦点断言保留。生产焦点行为未为测试强制改写；早期 `annotation-native-results.txt`、`annotation-native-focus-*.txt` 不能当通过记录。随后用户确认解锁，原生 `annotationTextEditKeepsTheEditorUsable` 补验 **3 passed / 0 skipped**（`annotation-unlocked-focus-results.txt`），通过 F2 修改、取消弹窗、返回画布焦点及继续绘制。Windows、完整辅助功能仍待验收。下一项独立实现为 ST-105。

独立界面复核确认线宽读数问题 `resolved`，最终 `ship` 仅覆盖这项修复，记录见 `.impeccable/review/annotation-edit-design-review.md`。原生焦点前置条件现在等待窗口激活再决定跳过，`annotation-focus-desktop-precondition.txt` 为 **2 passed / 1 skipped**，没有将锁屏状态计成功能通过。

2026-11-06 ST-105 首个增量：编辑器按 C 进入裁切，拖选保留区域，Enter、再次按 C 或主按钮确认；Esc／切工具取消预览。裁切按钮下拉只有水平／垂直翻转，保持图片贴图5、文字贴图4、托盘6个主入口，不新增右键菜单项。空区域或全幅裁切不产生历史，不清除 redo；有效裁切／翻转与标注修改共用一条撤销／重做记录。

ImageDocument 保留不可变原图、当前裁切区域／镜像方向及每个标注的创建画幅。合成按对象层序在原创建画幅绘制，再投影到当前画幅，保留旧文字换行、马赛克采样及翻转像素；新对象在当前画幅正向绘制。裁掉的对象仍属于文档，可撤销恢复；命中、选择框、移动按当前画幅映射，部分裁掉的对象不会在零位移时跳动。帧变更只记区域和方向，不保存整图撤销快照；合成仍需原图及临时图像缓冲，大图性能未单独验收。

复制、保存、快速保存、贴图与应用输出已提交图像，不含裁切框。文档实际修改及撤销重做更新 revision，取消编辑器等待中的旧 OCR 请求，防止旧结果回写；裁切预览与无变化操作不取消请求。应用到原贴图沿用 setImage 清除旧文字范围并重新识别，不改写源文件。对象、创建画幅及变换历史只在当前文档中保留，导出／历史／贴图会话仍为扁平图片；灰度／反色留待下一增量，ST-105 未全部完成。

本增量 Mac Qt 6.12 全量构建及最终非 integration CTest **12/12 组通过**（`cmake-build-debug/image-transform-ctest-final-results.txt`，35.76秒）：核心 **45 passed**、UI **64 passed**、设置 **78 passed / 12 环境 skipped**。覆盖11种标注／透明像素的逐像素裁切与镜像、变换后旧对象修改及新文字方向、撤销分支、预览取消、输出与异步 OCR 失效。首轮唯一失败为回归数据误用 Qt 负尺寸 QRect 的端点语义，已修正测试构造，未改生产裁切规则；保留 `image-crop-fixture-before.txt` 及首轮 CTest 日志，不将它们计为通过。

原生合成图像变换与12张中英深浅／正常紧凑窗口、工具栏和两行下拉菜单截图 **7 passed**（`image-transform-native-final-results.txt`），仅用隔离合成数据与窗口自身截图。界面复核发现线宽框禁用时仍显示成可编辑外观，已复用当前深浅主题的禁用文字颜色修正；之后全量构建通过、相关 UI／设置 CTest **2/2** 复验通过（`image-transform-disabled-final-results.txt`），重新捕获上述12张截图。独立终审确认禁用态问题 resolved、UI disposition 为 `ship`，记录见 `.impeccable/review/image-transform-design-review.md`，不扩大为全产品验收。

用户解锁后新增独立原生键盘回归 `imageTransformsKeepNativeKeyboardUsable`，通过 C 进入、Enter／再次 C 确认、翻转后继续裁切、平台撤销重做、Esc 取消预览及再次 Esc 关闭；原图与对象保留。`image-transform-unlocked-keyboard-results.txt` 为 **3 passed / 1 skipped**：裁切键盘用例实际通过，该批 F2 因首窗未处于前台而跳过，不能计作 F2 再次通过；F2 的独立解锁通过证据为上文 `annotation-unlocked-focus-results.txt`。新增用例离屏 **3 passed**（`image-transform-keyboard-offscreen-results.txt`），测试目标重编通过；未因只新增用例重复执行无关测试。Windows、完整键盘／辅助功能及大图性能仍待验收。总目标保持进行中，下一增量为 ST-105 灰度／反色。

2026-11-06 ST-105 第二增量：裁切按钮下拉新增灰度、反色，与水平／垂直翻转按几何和颜色分组，仍只有一层菜单，不增加图片／文字贴图或托盘主入口。两种效果处理当时当前画幅内的完整合成结果，包含已有标注，仅改变 RGB、保留 alpha；之后新增标注使用所选颜色，旧标注仍可移动、改样式或删除，修改后继续经过原有后续效果。灰度使用 Qt qGray，反色逐通道取 255 减原值，不覆写源文件。

ImageDocument 的标注创建上下文保留画幅和单调创建顺序；效果记录源图区域及创建顺序，渲染按两类顺序合并，同画幅批处理不会跨过效果。新增／替换／删除恢复、裁切／翻转与效果共用撤销记录，undo／redo 和分支不改变旧效果边界，不保存整图历史快照或扁平化对象。画布取消未提交的绘制／移动／裁切预览后再应用效果；复制、快速保存、贴图、应用及 OCR 使用处理后的已提交图，效果提交沿用 revision 取消旧 OCR 请求。

本增量 Mac Qt 6.12 全量构建通过（`color-effect-build.txt`）。非 integration CTest 首轮 **11/12 组通过**（`color-effect-ctest-results.txt`），唯一失败是新增菜单测试假设 End 键可选末项；没有生产图像处理失败。测试改用方向键逐项导航、跳过分隔线并 Enter 执行，重编设置目标后该组 **1/1 复验通过**（`color-effect-settings-final-results.txt`），其余11组源码未变，沿用首轮通过证据。最终分项为核心 **49 passed**、UI **69 passed**、设置 **85 passed / 12 环境 skipped**。保留首轮菜单失败 `color-effect-menu-keyboard-before.txt`，不把失败记录作为通过。

原生合成图像处理、旧标注修改／新彩色标注、菜单方向键／Enter 及12张中英深浅／正常紧凑截图 **8 passed / 0 skipped**（`color-effect-native-results.txt`）。截图位于 `.impeccable/review/qml-color-effect-{grayscale,invert,menu}-*.png`，仅用隔离合成内容与窗口自身截图。独立界面审阅 `ship`、无 material fixes，记录在 `.impeccable/review/color-effect-design-review.md`；仅覆盖本轮 UI，不替代功能或跨平台验收。核心还覆盖硬编码 RGBA、11 类工具、裁切／双翻转作者画幅、多效果混合、旧对象预览／修改／删除恢复及撤销分支；界面覆盖输出、未提交预览取消与阻塞 OCR 旧结果不回写。Windows、完整辅助功能和大图性能仍待验收；PNG/JPEG、历史和贴图会话仍只保存扁平结果。F23 的亮度尚未实现，下一独立增量继续补齐；总目标保持进行中。

2026-11-06 ST-105 第三增量：同一下拉菜单新增“调整亮度…”，不增加右键菜单或快捷键。弹窗支持 -100 至 100 的实时预览，负值变暗、正值变亮。效果按 `qRound(amount * 255 / 100.0)` 对 RGB 作饱和偏移，保留 alpha，与原有颜色处理共用创建顺序和源图区域。旧标注仍可编辑并经过原有后续效果，新标注使用所选颜色。

ImageDocument::previewBrightness 每次从已提交合成结果生成预览，不累积上次数值，不改原图、对象、revision 或撤销记录。Canvas 只持临时预览数值，rendered／复制／保存／贴图／应用和 OCR 仍使用已提交结果。弹窗结束先清预览；取消或确认 0 保留选择、裁切区域、redo 及等待中的 OCR，非零确认取消未完成手势并提交一次可撤销效果，沿用 revision 使旧 OCR 失效。关闭或重启仍不保存对象与处理历史，PNG/JPEG、历史和贴图会话保持扁平图片，不改写源文件。

本增量 Mac Qt 6.12 全量构建及非 integration CTest **12/12 组通过**（`brightness-build.txt`、`brightness-ctest-results.txt`，42.31秒）：核心 **51 passed**、UI **72 passed**、设置 **91 passed / 12 环境 skipped**。覆盖硬编码正负／极值／零值 RGBA、饱和截断、预览不改历史且等于提交结果、组合裁切／翻转／颜色效果、旧对象修改／删除恢复、新标注本色、输出、取消与零值保 redo，以及预览／零值不取消 OCR、真正提交阻止旧结果回写。

原生截图发现 Cocoa 深色弹窗的取消文字与步进箭头难辨，已将样式限制在亮度弹窗及其子控件内，使用 Fusion 遵循应用调色板；样式对象由弹窗持有，未改全局样式。修复后应用与相关测试目标重编通过（`brightness-dialog-final-build.txt`），UI／设置 CTest **2/2 复验通过**（`brightness-dialog-ctest-final-results.txt`，25.75秒），最终原生预览／提交、取消／零值、返回焦点／画布快捷键及12张中英深浅／正常紧凑截图 **8 passed / 0 skipped**（`brightness-native-final-results.txt`）。截图为 `.impeccable/review/qml-brightness-{dialog,preview,applied}-*.png`，两张 `dialog-before-*` 保留修复前对比；只使用隔离合成数据与窗口自身截图，不读取桌面或真实剪贴板。

独立界面终审确认深色弹窗可见性问题 resolved，最终 `ship` 仅覆盖本轮 UI，记录见 `.impeccable/review/brightness-design-review.md`；不以视觉结论代替功能或跨平台证据。ST-105 列出的裁切、翻转、灰度、反色和亮度已实现并完成以上 Mac 范围回归；Windows、完整辅助功能与大图实时预览性能仍待验收，不将当前结果扩大为完整跨平台验收。下一独立增量推进 ST-106；总目标保持进行中。

2026-11-06 ST-106 首个增量：托盘“更多操作 → 延时截图…”提供 1–60 秒、默认5秒的非模态倒计时，保留6个托盘主入口。开始等待后可切换到目标应用，截止时才调用原截图流程，检查权限、隐藏窗口并逐屏冻结后框选，继续沿用当前选区完成动作。取消按钮、窗口关闭与该窗口的 Esc 均同步停止计时，不产生后续截图；重复打开只激活原窗口，普通截图取消等待并立即接管。

`capture/capture_delay.*` 独立管理输入、单调截止时间与显示刷新，事件循环短暂阻塞不会累计计时漂移。`Application` 只管理单个窗口引用、完成交接和析构清理；等待期间不隐藏已有窗口、不请求截图权限、不采集帧。普通截图与滚动采集复用平台边界 `captureScreen(QScreen*)`，生产仍调用原 QScreen::grabWindow，测试替换为合成帧与调用计数，不读取真实桌面。倒计时窗口先关闭再进入捕获，不会随截图完成恢复。

本增量 Mac Qt 6.12 全量构建及非 integration CTest **13/13 组通过**（`delay-capture-build.txt`、`delay-capture-ctest-results.txt`，55.71秒）；核心 **51 passed**、UI **72 passed**、设置 **97 passed / 12 环境 skipped**、独立倒计时 **9 passed**。覆盖默认值与范围、等待时事件循环继续运行、真实截止时间、Return 不提前完成、取消／Esc／关闭后无迟到截图、阻塞后首次事件处理即结束、中英／主题更新不重计、重复入口、普通截图覆盖及 App 销毁清理。

原生补验复现了 Mac 工具窗口在应用失活时隐藏的问题：`delay-panel-before-results.txt` 的 `NSPanel.hidesOnDeactivate=true` 断言失败。已为倒计时窗口增加 `WA_MacAlwaysShowToolWindow`，保留局部 Fusion 深色可读性处理，不新增生产激活或抢焦点逻辑。修复后重编通过、倒计时／设置 CTest **2/2 复验通过**（`delay-panel-final-build.txt`、`delay-panel-ctest-final-results.txt`，35.51秒；设置 **97 passed / 13 环境 skipped**，新增的原生失活用例在离屏环境明确跳过）。

原生合成帧流程、销毁清理与最终12张中英深浅／正常紧凑输入、倒计时和菜单截图 **8 passed / 0 skipped**（`delay-capture-native-final-results.txt`）；独立倒计时原生测试 **9 passed / 0 skipped**（`delay-timer-native-final-results.txt`）。另注册独立串行 integration `still_mac_delay`，通过受控的第二测试进程与 AppKit 协作激活完成实际应用切换，断言目标进程激活、本进程失活后原生与 Qt 倒计时窗口均保持可见；CTest **1/1 通过**（`delay-cooperative-ctest-results.txt`，8.51秒），QTest **4 passed / 0 skipped**（`mac-delay-results.txt`）。辅助进程仅展示合成窗口，结束时清理；旧直接调用 deactivate 的失败批次只保留作诊断，未计为通过，也未将原因确定归于第二次创建 Application。

独立界面终审确认取消入口隐藏问题 resolved，最终 `ship` 仅覆盖本轮延时截图 UI，记录见 `.impeccable/review/delay-capture-design-review.md`；全部12张最终截图为 `.impeccable/review/qml-delay-capture-{input,countdown,menu}-*.png`。上述证据证明倒计时交接和受控跨进程失活时取消入口可用，不代表真实桌面／跨应用展开菜单的像素捕获、截图首次／拒绝／重新授权、Windows 或完整辅助功能已验收。ST-106 保持部分完成，上次区域、数字宽高与固定比例仍待实现；P0 原生待验收项继续保留，总目标保持进行中。

2026-11-06 ST-106 第二增量：托盘“更多操作 → 重复上次区域”在首次确认选区后启用，重新冻结同一源屏区域的新画面并沿用当前“选区完成后”动作；未新增根菜单项，托盘主入口仍为6项，更多分组现为6行动作。普通截图仍从空选区开始，取消、整屏截图与打开历史不覆盖已确认区域；重复截图会取消等待中的延时。首轮只保留当前运行的最后确认区域，不提供跨重启记录、固定区域或历史预设。

`Application` 单独持有源屏 `QPointer<QScreen>` 与本屏局部逻辑 `QRectF`，冻结帧携带源屏身份。Overlay 的 accepted 在第三增量中调整为精确本屏局部 `QRectF` 直传，避免全局平移往返的浮点损失；仅传给贴图／滚动等既有整数区域入口时加原点并取整。重复截图按源屏身份匹配新帧，复用 `cropFrame` 的实际像素／逻辑尺寸比例，屏幕移动只改变全局原点，不另乘 DPR，也不使用上次的旧像素。源屏不可用或旧区域不被当前逻辑屏幕完整包含时明确提示并继续普通框选，不静默换主屏或裁小区域。`deliverCapture` 按值保留交付数据后关闭 Overlay，再排队沿用原有输出流程。

Mac Qt 6.12 应用与相关测试目标构建通过（`repeat-region-build.txt`、`repeat-region-native-final-build.txt`）；UI CTest **1/1 通过**（`repeat-region-ui-ctest-results.txt`，3.64秒），**73 passed / 0 skipped**。新增真实小数鼠标事件覆盖负屏原点、1.5倍像素映射与硬编码裁切像素，取消不发送 accepted。最终设置 CTest **1/1 通过**（`repeat-region-settings-final-ctest-results.txt`，36.91秒），**102 passed / 14 环境 skipped**（`repeat-region-settings-final-results.txt`）。覆盖首次禁用／确认启用、新帧颜色与精确区域、320像素上限、当前编辑／贴图动作、普通截图仍框选、取消／整屏不覆盖、重复接管延时及迟到截止不会再次截图。英文文案检查 **390 source strings / 415 catalog entries** 通过。

最终原生流程与12张中英深浅／正常紧凑禁用菜单、启用菜单及失效提示 **7 passed / 0 skipped**（`repeat-region-native-final-results.txt`，15.359秒），只用隔离合成帧与自身窗口内容。布局缩小由旧区域状态模拟；提示关闭后重新框选、确认合法区域再重复输出已覆盖，不将此测试当作真实屏幕热插拔。消息框使用实际系统 NSAlert；测试在原生模态循环内匹配正文、渲染其内容视图并关闭。透明区域按系统窗口底色铺底供文字／按钮复核，不能证明系统合成器或玻璃背景；四张提示实际均为系统浅色外观，不据此宣称系统深色提示已验收。最终测试驱动的相关离屏复验 **7 passed / 0 skipped**（`repeat-region-driver-offscreen-final-results.txt`，7.707秒）。

早期设置失败记录 `repeat-region-settings-first-results.txt` 中，四个提示场景的标题断言不符合 [Qt 的 macOS 消息框契约](https://doc.qt.io/qt-6/qmessagebox.html#setWindowTitle)，已校正平台断言，正文与重选流程断言保留；另两项旧用例失败在后续两次整组运行均未再出现，未坐实其原因、未据此修改生产行为。原生截图首轮因 Qt 定时器驱动无法关闭 NSAlert 而停驻，保留 `repeat-region-native-message-driver-before.txt` 与调用栈 `repeat-region-native-sample.txt` 为诊断，未计为通过。后续仅为测试增加有时限的原生模态驱动，没有将生产消息框换成 Qt 窗口以绕过验证。

独立界面终审逐张复核最终12张截图，结论 `ship`、无 material fixes，仅覆盖本轮区域复用 UI；记录见 `.impeccable/review/repeat-region-design-review.md`，截图为 `qml-repeat-capture-{menu-disabled,menu-enabled,unavailable}-*.png`。未打包测试进程的系统文件夹图标不作为产品 bundle 图标结论；打包后原生提示、真实热插拔／混合 DPI 桌面、跨应用展开菜单像素、权限流程、Windows 与完整辅助功能仍待验收。ST-106 保持部分完成，下一独立增量为数字宽高，再补固定比例；总目标保持进行中。

2026-11-06 ST-106 第三增量：框选工具条提供宽、高两个数字框，单位为最终图像像素，范围1到当前冻结帧整幅；修改一维保留另一维，碰到右侧／下侧只平移选区以保留输入尺寸。鼠标、八手柄和键盘调整同步读数，输入框 Enter 只提交数值，点击输出按钮会先提交尚未失焦的输入；回到选区 Enter 完成，Esc 取消。快速复制／长截图默认仍松手即完成，按住 Shift 松手可进入精调，之后移动／改已有选区不会提前输出。工具条仅增加尺寸行，原8操作、32px按钮／20px图标及长截图42px操作条保持；托盘主入口未增加。

几何仍只有 `Overlay::selection_` 一个所有者。`imaging/image.cpp` 的 `logicalRect` 按实际像素／逻辑比例逆映射整数像素矩形，并向内校正已复现的浮点边界误差，不改变 `pixelRect` 原有向外取整规则。覆盖除法边界、矩形重建、1px、整幅与贴右下边等情形；5组比例共1125个边缘矩形逐一验证范围和精确往返，另验证实际裁切图像。accepted 直接保留本屏局部 `QRectF`：负屏原点的全局平移往返曾吞掉边界余量，区域复用现不经过该往返；原生回归通过实际框选／输入401×257再重复输出验证新帧尺寸与内容。

排查并修复两项交互回归，均保留修复前失败：已有1px窄选区移动不能再触发新框选的3DIP点击吸附阈值（`capture-dimensions-thin-before-results.txt`）；copy／scroll 精调后普通移动不能提前 accepted（`capture-dimensions-adjust-before-results.txt`）。输入焦点先提交再开始画布手势，工具条鼠标事件不穿透，避免清空后迟到提交复活选区。输入焦点期间截图快捷键不触发输出，Tab／Return／Esc及按钮提交均有回归。

原生视觉检查发现浅色选中文字对比不足、Cocoa DPR 1 的步进箭头消失。前者改为深色选中文字；后者由 Qt QSS 回退绘制在默认14px按钮内缩后没有足够箭头宽度导致，仅给两数字框的步进按钮局部设18px宽，不改全局或换成 Fusion。测试实际检测上下箭头像素；临时在测试中恢复14px得到 DPR 1 失败、DPR 2 通过（`capture-dimensions-arrow-before-results.txt`），恢复最终几何后两屏均通过。不能以离屏可见代替原生箭头验证。

应用／UI／设置目标最终重编通过（`capture-dimensions-polished-build.txt`），核心目标构建见 `capture-dimensions-core-final-build.txt`。最终相关 CTest **3/3 通过**（`capture-dimensions-polished-ctest-results.txt`，43.26秒）：核心 **60 passed**、UI **87 passed**、设置 **108 passed / 14 环境 skipped**，逐组日志为 `capture-dimensions-{core,ui,settings}-polished-results.txt`。英文文案与占位符检查 **394 source strings / 421 catalog entries** 通过（`capture-dimensions-translation-results.txt`）。

最终 Mac Qt 6.12 原生输入／焦点、4组中英深浅及正常／紧凑截图、两块屏幕实际 DPR 1／2 的箭头绘制、区域重复与长截图工具条 **12 passed / 0 skipped**（`capture-dimensions-polished-native-results.txt`，12.431秒）。测试仅使用隔离合成帧及窗口自身内容；原生前台条件通过普通打开剪贴板入口建立，未增加生产激活逻辑。最终16张截图为 `.impeccable/review/qml-capture-dimensions-{ready,selection,focused,fullframe}-*.png`，4张 `before-*` 保留修复前对照。

独立界面终审逐张复核最终16张截图及4张修复前对照，结论 `ship`，两项 material 问题均 resolved，无待修项，仅覆盖本轮宽高输入 UI；记录见 `.impeccable/review/capture-dimensions-design-review.md`。上述真实屏幕 DPR 校准只验证 Widgets 绘制，不代表真实桌面混合 DPI 像素捕获；Windows、真实热插拔、截图权限、打包应用与完整辅助功能仍待验收。ST-106 保持部分完成，下一独立增量为固定比例；P0 未验收项和总目标继续保持进行中。

2026-11-06 ST-106 第四增量：宽高旁增加比例选择，默认“自由比例”，提供1:1、4:3、16:9、3:4、9:16；仅提供当前帧可容纳最小整数单位的选项。比例属于当前 Overlay，不增加持久配置、预设管理或托盘主入口。切比例按当前宽度调整；固定比例下宽高按共同整数倍率就近对齐，范围与步进同步，受整幅上限约束。切回自由保留当前选区，恢复1px步长与全幅范围。快速复制／长截图仍可按住 Shift 松手精调，已有选区调整不会提前输出。

固定比例约束覆盖数字输入、新框选、八手柄、内部移动、窗口吸附及键盘。几何仍只有 `selection_` 一个所有者，先约束整数像素矩形，再经 `logicalRect` 写回；四角保留反角，四侧保留反边中点，跨侧可翻转，不能容纳最小单位时停在上一有效区域。鼠标缩放按锚点空间限制倍率，数字调整与移动夹住位置而不裁小尺寸。方向键增减一个比例单位，Shift为十个；Alt按输出像素平移，Shift+Alt为十个像素。原8操作、32px按钮／20px图标、长截图42px操作行及6个托盘主入口保持。

独立逻辑复核发现并修复两处边界，均有有效修复前失败与最终回归：320×224像素／1000×700逻辑尺寸下，逐次取整的 Alt 微调会停住，改为整数像素平移（`capture-ratio-alt-before-results.txt`，8行失败）；悬停窗口预览曾直接画未约束窗口，改为绘制与确认共用 `hoverSelection`（`capture-ratio-preview-calibration-results.txt`，恢复旧绘制分支时失败）。首轮 hover 测试在修复后仍因相同全局位置的 QTest::mouseMove 未送事件失败，已使用明确局部鼠标事件固定驱动，并重新校准旧分支失败；不把驱动问题当作新的生产缺陷。最终UI覆盖五比例、负屏原点、1.5倍／非等比缩帧、真实键入与回显、最小单位、整幅上限、四角／四边／跨反侧、移动／键盘／边界、清空重画、吸附和快模式。

原生界面复核修复两项可见问题：深色比例框箭头黑底难辨，仅给新控件增加局部 QSS 前景／背景／边框（`capture-ratio-arrow-before-results.txt` 保留失败）；下拉首项“自由比例”／“Free ratio”实际截字，使用真实字体文字尺寸与平台 `CT_MenuItem` 计算最小宽度（`capture-ratio-popup-text-before-results.txt`，viewport65小于独立文字下界69）。不新增自绘控件、子类或替身弹层。箭头回归按内部像素与背景至少3:1对比度验证；初版要求接近纯前景色使离屏抗锯齿小箭头误失败，已按实际视觉条件校正，保留 `capture-ratio-arrow-probe-results.txt` 为诊断。首轮 CTest 2/3 的失败仅在该判定；后续离屏又锁住了真正的菜单宽度问题，没有跳过新增断言。

Mac 本机的比例菜单实际走标准 QComboBox 的 Qt 弹层，日志明确 backend=Qt，不是 NSMenu；早期根据通用源码猜测而写的未使用 NSMenu 驱动已删除，未据此修改生产弹层。当前测试对实际菜单检查语言、委托／文字宽度下界、窗口抓图及 Esc 关闭，并确认弹层关闭不取消截图、再次 Esc 才取消。原生首轮 `capture-ratio-native-first-results.txt` 有一处 Tab 断言失败：Qt 的 [Mac ComboBox 默认策略](https://github.com/qt/qtbase/blob/v6.12.0/src/widgets/widgets/qcombobox.cpp#L1203-L1212) 遵循系统全键盘导航偏好。测试按实际偏好保留 Tab 到达或跳过断言，再验证比例框 Return／快捷键；没有改生产焦点或系统设置。

应用、核心及UI／设置目标最终重编通过（`capture-ratio-popup-final-build.txt`）；最终相关 CTest **3/3 通过**（`capture-ratio-ctest-final-results.txt`，42.57秒）：核心 **60 passed**、UI **125 passed**、设置 **113 passed / 14 环境 skipped**，逐组日志为 `capture-ratio-{core,ui,settings}-final-results.txt`。文案与占位符检查 **397 source strings / 424 catalog entries** 通过（`capture-ratio-translation-results.txt`）。最终 Mac Qt 6.12 原生 **13 passed / 0 skipped**（`capture-ratio-native-final-results.txt`，8.570秒），覆盖比例与宽高输入、四组截图／真实 Qt 下拉、实际两屏 DPR1／2 的箭头、401输入对齐400×225后按新帧重复区域、长截图操作行。

最终12张截图为 `.impeccable/review/qml-capture-ratio-{selection,focused,dropdown}-*.png`；两张 `before-*` 保留深色箭头对照，四张 `dropdown-before-*` 保留首项截断对照。独立界面终审逐张复核最终12图及6张对照，结论 `ship`，两项 material 问题均 resolved，无待修项，仅覆盖本轮比例工具条／实际 Qt 弹层；记录见 `.impeccable/review/capture-ratio-design-review.md`。全部图像输入为隔离合成帧，抓图限于自身窗口，DPR校准只证明 Widgets 绘制；Windows、真实混合DPI桌面捕获／热插拔、权限、打包与完整辅助功能仍待验收。ST-106 的数字宽高与固定比例范围已交付，F03固定区域／历史预设仍未实现，下一独立增量继续补区域预设；总目标保持进行中。

2026-11-06 ST-106 第五增量：托盘“更多操作 → 区域预设…”提供非模态工具窗口，为上次确认区域命名保存、选择截取及删除确认。支持跨 App 重启复用，保存时名称去首尾空白、最多80个UTF-16单元，区分大小写且同名拒绝覆盖。名称 Enter 保存，列表 Enter／小键盘 Enter／双击截取，焦点按钮 Enter 执行对应动作；空列表 Enter 不会误保存输入。托盘仍6个主入口，更多分组由6变7项，不增加预设子菜单。截取前管理窗口先隐藏，避免输出时被抬回前方。

预设只保存屏幕身份和精确本屏逻辑 `QRectF`，不保存图像、输出动作或比例配置；每次调用重新冻结，按当前屏幕尺寸／实际帧尺寸和最大边长设置裁切，使用当前“选区完成后”动作。成功调用成为新的“重复上次区域”，调用时会取消延时，迟到截止不再次截图。`Application` 只维护一份最后区域业务记录；冻结帧携带身份值快照，避免提示期间拔屏后再解引用空QPointer取身份。匹配先检查全部当前屏幕的唯一身份，再找成功冻结帧；序列号存在时要求序列号／厂商／型号相同，无序列号要求非空名称及名称／厂商／型号全同。身份缺失／不明／歧义或完整区域放不下时明确提示并进入普通框选，不选择主屏、不裁小旧区域；取消重选不覆盖上次合法区域。

独立 `region-presets.json` 固定在 App 数据根目录，版本1；普通设置提交、历史／剪贴板目录迁移和退出不重写或迁移该文件。完整加载验证结构、版本、唯一名称及有限非负坐标／正尺寸，保留double；文件缺失为空表，不可读、损坏、未知版本或任一无效条目均拒绝整表加载／覆盖。每次写前复验旧文件，再用现有 `writeJson`／QSaveFile 原子提交；列表先构造候选、写成功才更新，失败保留输入、选择和原字节。品牌数据目录检测包含该文件，Windows隔离路径回归在本机明确跳过，不伪称已实测。

本轮保留两处确定修复前失败：关闭后在deferred delete前立即重开曾复用待删窗口，现App在finished时清指针（`region-presets-reopen-before-results.txt`）；带文件路径的整串错误无法动态翻译，现翻译固定原因后另附原路径（`region-presets-dialog-before-results.txt` 的语言行）。错误来源由窗口区分：磁盘错误附路径，重名只给用户可操作提示，外部损坏后的写失败仍保留路径与旧列表。最初键盘测试将同一工具窗口hide后立即重show，在离屏环境未能建立全局激活前置条件；已拆成独立新窗口的两数据行，保留激活和焦点断言。随后明确复现了真正的列表 Enter 行为差异：Qt Mac item view 默认将 Return 用于编辑，现增加仅列表生效的标准 QShortcut，复用截取按钮启用状态；`region-presets-resume-ctest-results.txt` 为修复前5/6记录。未增加生产激活补丁、跳过焦点断言或替换原生提示。

应用及相关目标重编通过（`region-presets-resume-build.txt`、`region-presets-keyboard-error-build.txt`）。最终相关 CTest **6/6 通过**（`region-presets-final-ctest-results.txt`，48.38秒）：核心 **60 passed**、预设存储 **52 passed / 1 Windows skipped**、管理窗口 **17 passed**、设置应用 **10 passed**、已有UI **125 passed**、设置集成 **119 passed / 16 环境 skipped**。逐组日志为 `region-presets-final-{core,region-presets,capture-presets,settings-apply,ui,settings}-results.txt`。文案及占位符检查 **414 source strings / 441 catalog entries** 通过（`region-presets-translation-results.txt`）。新增存储测试覆盖整表损坏／未知版本／错误类型／几何／名称、失败拒写原字节、Unix读写权限故障、父路径被占用及屏幕唯一匹配；窗口测试覆盖增删、取消确认、写失败、用户名称不翻译、语言切换、Enter与关闭不改文件。

本机 Mac Qt 6.12 原生 **13 passed / 0 skipped**（`region-presets-native-first-results.txt`，17.959秒，含初始化／清理）。覆盖实际可识别屏幕的Overlay确认、201×127像素精确区域保存、新帧调用、Repeat、App重建后恢复、当前贴图动作、320上限、设置及双目录迁移不改预设、延时接管与迟到防重；缺屏／越界提示、取消后旧Repeat保留和旧长截图操作行均通过。离屏环境没有持久屏幕身份，正向跨重启匹配和越界两个集成场景明确跳过，已由上述原生结果补验；不将跳过当完成。

最终13张自身窗口截图为 `.impeccable/review/qml-region-presets-{empty,saved,error}-*.png` 12张与 `qml-region-presets-menu-normal-light-zh.png`。面板使用既有局部Fusion模式和MacAlwaysShowToolWindow，中英深浅、正常560×420／紧凑440×340逻辑尺寸均覆盖；实际PNG为2倍像素。独立界面终审逐张检查全部13图，结论 `ship`、无 material fixes，仅覆盖本轮管理窗口及所提供More菜单；记录见 `.impeccable/review/region-presets-design-review.md`。用户名称“复制”在英文界面保持中文是保留用户内容，错误原因与路径完整换行；根菜单6项另由源码／测试证明。

本轮以用户命名的跨运行预设完成“历史选区/预设复用”的预设支路，不宣称自动收集全部最近选区或提供独立历史列表。ST-106列出的本阶段最小功能已实现并完成以上Mac范围回归；Windows、真实桌面／混合DPI像素捕获、热插拔、权限、打包和完整辅助功能继续待验收，不能标记整项跨平台验收完成。全部图像输入为隔离合成帧，原生截图限自身窗口；屏幕身份与失效场景的测试不等于真实热插拔。下一独立增量按顺序推进 ST-107，保留P0待验收项，总目标继续进行中。

2026-11-06 ST-107 首增量：新增共享非模态 OCR 结果窗口，分别呈现可编辑校对稿与只读原始结果。截图 OCR 和编辑器“复制文字”识别成功后保留立即原样复制，再打开独立快照；失败或无文字仍按既有流程提示，不覆盖剪贴板。图片贴图仅在“更多操作 → 校对识别文字…”打开当前结果，不增加主菜单项，自动识别不弹窗。校对不改图片、标注、贴图文字范围或位置框；旋转／编辑／重新识别之后，已打开快照保持原来的内容，迟到请求沿用已有取消／revision 校验。

换行提供保留／转空格／移除，水平空白提供保留／连续合并／移除，均默认保留；切换选项不改稿，点击“应用整理”才处理当前校对内容，不自动去首尾空白或改标点。整理、还原原文分别使用一次 Qt 编辑块并可撤销，原文始终可查看、复制；关闭不持久保存校对稿。两页使用不自动折行的标准纯文本控件，复制按钮按当前页选择复制选区或全文。局部 `createMimeDataFromSelection` 保证按钮、标准快捷键／菜单和拖出只交付 `text/plain`；系统拖动沿用编辑稿 Copy／Move、原文只读 Copy 的 Qt 行为，移动可撤销，没有另建拖拽框架。

独立逻辑复核发现并修复 raw 文本与显示稿混淆：Qt 的 `toPlainText` 规范化 NBSP、CRLF 及段落分隔，原先按“显示稿相等”复用原文，会吞掉用户明确输入的 ASCII 空格，也让仅空格整理改变保留的 CRLF。两项修复前失败见 `ocr-result-raw-before-results.txt`（**2 passed / 2 failed**）；现将初始／整理／还原的 raw 快照绑定到既有 Qt 文档撤销位置，新分支清理旧 redo 快照，合并输入使对应快照失效，普通修改使用文档 raw 字符。没有第二套编辑或撤销栈；复核后的代码未发现新确定缺陷。全文及全选复制保持对应 raw 状态，部分选区使用文档字符并将内部段落分隔转 LF。

应用和相关目标构建通过（`ocr-result-first-build.txt`、`ocr-result-raw-fixed-build.txt`、`ocr-result-settings-driver-build.txt`）；最终相关 CTest **3/3 通过**（`ocr-result-final-ctest-results.txt`，51.05秒）：结果窗口 **24 passed**、原有UI及新增入口 **126 passed**、设置集成 **126 passed / 17 环境或显式手工验收 skipped**。逐组日志为 `ocr-result-final-{dialog,ui,settings}-results.txt`。文案及占位符检查 **436 source strings / 463 catalog entries** 通过（`ocr-result-translation-results.txt`）。测试覆盖九种整理组合、Unicode水平空白／换行、未改稿原样复制、同显示字符修改、当前校对稿、整理／还原／分支撤销、HTML粘贴仅取纯文本、语言主题不改文本／选择／选项、Enter输入换行及关闭；入口回归覆盖重复请求、图像变换取消旧任务、贴图校对不重识别／不改变选字范围、两次结果快照互不覆盖及App销毁关闭窗口。

首次整组执行停在无系统托盘时的真实 `QMessageBox::warning`，原因是新增错误场景缺少测试关闭驱动；已只在无托盘环境用有界定时器匹配实际正文后关闭并断言，不改生产提示。原始中断记录为 `ocr-result-ctest-driver-before-results.txt`，不计通过；修正后上述最终整组通过。本机 Mac Qt 6.12 原生入口与四组中英深浅／正常紧凑交互 **9 passed / 0 skipped**（`ocr-result-native-final-results.txt`，4.112秒，含init／cleanup），只用隔离合成文字与自身窗口；通过真实Overlay完成OCR动作、实际键入／焦点、编辑／原文复制、显式整理、独立第二结果和关闭。此处 OCR 引擎由测试边界返回固定结果，不据此宣称真实识别质量已重新验收。

最终12张原生自身窗口截图为 `.impeccable/review/qml-ocr-result-{editable,original,formatted}-{normal-light-zh,normal-dark-en,compact-light-en,compact-dark-zh}.png`，正常680×520／紧凑520×420逻辑尺寸，本机PNG为2倍像素。格式化后长行不自动折行，水平滚动条保留完整内容；截图的视口随末尾光标横向滚动不表示丢字。界面沿用现有局部Fusion样式和Mac工具窗口约定。独立界面终审逐张复核全部12图，结论 `ship`、无 material fixes，仅覆盖所提供本轮UI；没有before图，不宣称视觉改善幅度，记录见 `.impeccable/review/ocr-result-design-review.md`。

跨窗口拖出保留单独显式门控 `ocrResultDragHandoff`（`STILL_OCR_DRAG_HANDOFF=1` 且 Cocoa，普通CTest明确跳过），等待实际鼠标拖到独立接收窗口并验证 Copy／Move、撤销与原文。无bundle CLI进程不能被当前CUA工具绑定；临时测试bundle可操作源窗口，但拖到独立接收窗口时工具返回 `windowNotFoundAtPosition((859.0, 284.0))`，只读系统状态未显示锁屏。两次等待进程已按实际PID终止，日志 `ocr-result-drag-unbundled-results.txt` 与 `ocr-result-drag-native-results.txt` 只有就绪前置条件，不计拖出通过，也不据此修改生产拖动。ST-107保持部分完成，跨窗口／跨应用拖出、Windows和完整辅助功能仍待实测；保留该验收项，下一独立增量推进 ST-108，总目标继续进行中。

2026-11-06 ST-108 首增量：通用设置底部新增单个“配置备份与恢复…”入口，不增加托盘菜单。管理窗口提供导出、选择备份、具体前后差异预览、严格导入及恢复全部已支持偏好的默认值。导入前默认保留本机 save/history/clipboard 目录，备份路径仅作为可见元信息；明确勾选“同时恢复目录”后才检查本机绝对路径并沿用现有目录事务。全部默认保留本机目录，确认默认“否”，明确说明替换未保存输入及降低配额可能清理旧内容，不声称恢复默认不会清理历史。

`settings/settings_backup.*` 使用 `{format:"still-settings",version:1,settings:{...}}` 完整已支持字段契约，复用表单元信息并包含实际生效的隐藏值 `capture.maxEdge`、`pin.confirmDoubleClickClose/opacity`、`scroll.maxHeightPx/maxFrames`。完整校验结构／版本、已知字段、类型、范围、枚举、模板、单组合及重复快捷键；任一无效或缺失字段拒绝整表。未知及内部字段不导出，导入时当前未知字段仍保留；不备份图像、剪贴板内容、贴图会话、区域预设或密钥。导出复用 QSaveFile 原子写入；备份字段集后续变更必须处理版本兼容。

`SettingsApply::applyBackup` 忙时拒绝且不发会干扰原请求的完成信号；任一快捷键预留失败则全部释放，不迁移、不写配置。全部成功预留后复用普通保存的 History→Clipboard→QSaveFile 单一提交检查点，失败保留原文件、注册及数据目录。普通 `apply` 的部分快捷键失败／其余设置保存和排队行为保持。真实默认快捷键表已从 App 静态函数移到 settings 统一复用，避免将全部默认误恢复成空快捷键；两个当前动作直接互换占用按键仍作为明确冲突拒绝，不先释放旧键。管理窗口用 modal exec 保护批量替换，成功后 `SettingsDraft::reloadSaved` 同步已确认替换的未完成输入，旧草稿不再重新覆盖新配置。

独立逻辑复核发现并修复首次运行的配置覆写漏洞：当前配置尚不存在时，同目录仅大小写不同的导出目标没有 canonical leaf，原保护会在不区分大小写卷上写入备份 envelope。修复前 `settings-backup-case-before-results.txt` **2 passed / 1 failed**；现保留直接路径／既存 canonical 别名检查，Mac 对既存父目录用 `pathconf(_PC_CASE_SENSITIVE)` 查询实际规则，不把所有卷假定为不区分大小写；无法判断则明确拒写。新增敏感卷允许大小写不同目标的回归，在本机不敏感卷明确跳过；Windows 特殊大小写敏感目录未验收。

应用及相关目标构建通过（`settings-backup-final-build.txt`、`settings-backup-polished-build.txt`、`settings-backup-focus-build.txt`）。相关 CTest 首次完整 **6/6 通过**（`settings-backup-final-ctest-results.txt`，60.91秒）；后续仅修改窗口样式及其测试驱动，最终相关重验 **2/2 通过**（`settings-backup-release-ctest-results.txt`，53.06秒）。当前六组源码对应结果为：备份格式 **189 passed**、设置事务 **22 passed**、草稿 **9 passed**、管理窗口 **14 passed / 1 真实卷条件 skipped**、原有UI **126 passed**、设置集成 **131 passed / 17 环境或显式手工验收 skipped**；无失败。逐组保留 `settings-backup-final-{codec,apply,draft,dialog,ui,settings}-results.txt`。文案及占位符检查 **481 source strings / 508 catalog entries** 通过（`settings-backup-translation-results.txt`）。覆盖严格整表读取、有效隐藏值、未知内容不导出、文件写失败、快捷键冲突整次拒绝／预留释放、旧普通保存行为、目录迁移失败回滚、默认目录保留、显式恢复、取消、草稿替换及原配置文件保护。

本机 Mac Qt 6.12 最终原生 **7 passed / 0 skipped**（`settings-backup-release-native-results.txt`，4.734秒，含初始化／清理）。通过真实 QML 入口、文件名键入和实际保存／打开按钮、导出后重新解析、默认“否”、导入、当前 General 页切换英文／深色后渲染、真实切页及 App 重建；验证历史 PNG、剪贴板索引与内容、区域预设及源备份原字节保持，默认不创建另一设备路径。复选框实际点击选中、焦点与 Space 取消及前后预览均通过。文件及确认面板在测试中限定为 Qt 后端，只证明 Qt 自身窗口流程，不宣称系统 NSOpenPanel／NSAlert 或其按钮本地化已验收。

保留验证过程中的失败证据：最早离屏导出解析失败未单独坐实原因，未据此改生产；后续原生 `selectFile` 选中路径不符 **2 passed / 5 failed**（`settings-backup-native-chooser-before-results.txt`），与 [Qt 6.12 可见文件名输入框有焦点时保留当前输入的实现](https://github.com/qt/qtbase/blob/v6.12.0/src/widgets/dialogs/qfiledialog.cpp#L1015-L1045) 一致。驱动现分阶段确认 Qt 输入框、实际键入、等待精确路径与按钮启用、实际点击，并在接受后复核路径。旧四行矩阵单独可复现 QtQuickLayout 崩溃而单行通过，见 `settings-backup-snapshots-repro-results.txt`；测试在状态切换间处理事件／deferred delete并完成实际 frame polish，仍保留 General 页真实外观导入场景，未增加生产布局／激活补丁。新键盘验证在离屏文件面板关闭后曾因父窗口未激活出现四行焦点失败（`settings-backup-focus-before-results.txt`）；只在测试补标准窗口激活及断言，最终离屏和原生同样通过，不跳过焦点断言。

独立界面终审逐张检查首批16图，唯一 material 为可用未选目录复选框边界过淡。修复前像素回归 **2 passed / 2 failed**（`settings-backup-indicator-before-results.txt`），浅／深边缘对比分别约1.65／1.05；只给本窗口 enabled+unchecked 指示框增加主题中性边界，保留原生 checked／disabled／focus。最终两主题四边至少3:1的断言通过。复拍初始／预览8图并增加选中4图后，同一审阅者逐张核对12张相关图，唯一问题评分 `resolved`，`ship` 只覆盖该修复与原本轮UI范围。最终共20张 `.impeccable/review/qml-settings-backup-{entry,initial,preview,confirmation,directories}-{normal-light-zh,normal-dark-en,compact-light-en,compact-dark-zh}.png`，8张 `before-{initial,preview}` 保留对照；入口1100×760／800×620、窗口700×580／560×500逻辑尺寸，PNG为2倍像素。记录见 `.impeccable/review/settings-backup-design-review.md`。

ST-108 本增量交付本地备份、严格导入和全部默认，不宣称已提供单项默认。下一独立增量继续补单项恢复默认；Windows、真实系统文件／确认面板、完整辅助功能及既有 P0 待验收项保持，总目标继续进行中。

2026-11-06 ST-108 第二增量：当前设置表单每个真实字段行新增单项恢复，预留36px固定位置，已更改时悬停或键盘聚焦才显示；默认项不可触发，不增加菜单入口。键盘遵循系统导航规则，支持 Tab／Space。快捷键恢复使用共享 `defaultHotkeys` 的实际默认值；内部配置归一化旧语义和备份版本1字段集保持不变。未在表单显示的五个偏好仍通过全部默认处理，不新增隐藏项列表。

`SettingsDraft::restoreDefault` 只提交目标字段，复用原保存队列、快捷键冲突和失败回滚，保留其他未完成草稿；不全表提交或重载。MainWindow 对历史／剪贴板目录迁回默认位置、配额低于已保存或草稿值的恢复使用默认No确认。其他文本框正常失焦自动保存仍按原规则执行；保存目录只影响未来输出，不按历史迁移处理。

输入生命周期回归实际暴露了确认前清理风险：已保存配额500、正在输入1，旧鼠标恢复先失焦提交1；`single-default-quota-before-results.txt` **2 passed / 2 failed**，取消行确认前值已变成1。恢复按钮现采用 `Qt.TabFocus`，本行恢复期间抑制模态失焦提交；数字框用 clear／insert 暂存未提交文本，取消后还原，避免破坏绑定或提前修改内部值。新增取消、确认、取消后Enter、已保存默认但仍有raw输入四行，检查确认前后原PNG字节；目录同样实际键入新路径，确认前不迁移，取消保留输入。另一条已确认RED是模板已默认但有非法草稿时恢复仍残留校验错误，见 `single-default-template-before-results.txt`；现仅清同字段本地校验来源，不抹后端失败、其他字段错误或正在保存状态。

应用及相关目标构建通过（`single-default-model-final-build.txt`、`single-default-input-build.txt`、`single-default-ui-synced-build.txt`、`single-default-appearance-build.txt`）。相关六组分批验证均通过：草稿／备份／管理窗口 **3/3**（`single-default-model-ctest-results.txt`），设置事务／原UI **2/2**（`single-default-connected-ctest-results.txt`），最终完整设置 **1/1**（`single-default-release-settings-ctest-results.txt`，58.82秒）。逐组结果 `single-default-final-{settings-draft,backup,backup-dialog,apply,ui,settings}-results.txt` 分别为 **25／189／14／22／126／141 passed**，无失败；管理窗口另有1项真实敏感卷条件 skipped，设置另有17项既有环境或显式手工门控 skipped，不将跳过计通过。最终文案占位符检查 **485 source strings / 512 catalog entries** 通过。

本机 Mac Qt 6.12 最终原生 **12 passed / 0 failed / 0 skipped**（`single-default-final-native-results.txt`，5.660秒，含初始化／清理）。真实鼠标输入、Tab／Space、重复恢复、快捷键冲突、其他草稿保留、目录迁回、确认取消及配额输入四行均通过；历史 PNG、剪贴板内容及区域预设按原字节核对。键盘用例仅在自身范围启用 `TabFocusAllControls` 并恢复旧 hint，不改系统偏好或生产焦点。确认窗口由测试限定为 Qt 后端，不代表系统 NSAlert／NSOpenPanel 已验收。

保留测试驱动的失败记录：`single-default-input-after-results.txt`／`single-default-native-first-results.txt` 暴露首次布局未完成就计算滚动、相同鼠标位置跨窗口未建立新悬停及剪贴板界面快照晚于设置完成的问题；补实际 frame polish、移出再进入、真实窗口激活与异步目录等待后，离屏功能 **8 passed / 0 skipped**（`single-default-input-synced-results.txt`）。原生 Tab 初次受系统只导航文本的规则影响，见 `single-default-native-tab-before-results.txt`。后续两行英文截图仍失败，抓图实际回退到中文浅色：测试只临时改外观，文本保存又应用真实旧配置，导致重建与失焦；`single-default-native-{space,appearance}-before-results.txt` 不计通过。最终截图改由真实设置流程保存语言／主题，先等待目标值与实际界面一致，结束也正常恢复；未追加生产布局、激活或外观补丁。

最终12张图为 `.impeccable/review/qml-single-default-{hover,focused,confirmation}-{normal-light-zh,normal-dark-en,compact-light-en,compact-dark-zh}.png`。自身窗口逻辑尺寸1100×760／800×620，PNG为DPR2；确认框按内容自适应。根代理和独立终审均逐张查看，终审 `ship`、无 material fixes，仅覆盖本轮提供的UI，记录见 `.impeccable/review/single-default-design-review.md`；不扩大为跨平台或完整辅助功能验收。当前表单单项默认的最小增量已交付，Windows、真实系统面板及既有未验收项继续保留。按最新目标只继续P1，下一独立增量为 ST-109，P2／P3不自动开发。

2026-11-07 ST-109 首增量：通用偏好顶部新增“登录后启动”，当前接入 macOS 13+ 的 `SMAppService.mainAppService`。查询和启动应用本身不登记；仅用户操作才启停。开关只在真实 Enabled 时开启，RequiresApproval 显示尚未允许且保持关闭，提供系统登录项和取消登记；NotFound 可以尝试重新登记或取消。显示／激活／回到General时重读，不轮询，不以旧JSON字段推断状态。系统本机登记不进入普通偏好、草稿或版本1备份，导入和恢复默认不自动触碰登录项；其他平台目前由明确Unavailable的stub禁用入口，不假称Windows已接通。接口语义以 [Apple SMAppService](https://developer.apple.com/documentation/servicemanagement/smappservice?language=objc) 及本地SDK为依据。

MainWindow只缓存本行系统状态及操作错误，StartupRow沿用既有单列表单、共享开关和按钮，不加菜单。真实外部状态改变会清除旧操作错误：修复前 `startup-error-before-results.txt` **2 passed / 1 failed**，失败后从另一个实际QWidget返回，开关已开启但旧错误仍遮住新状态；现只在状态确实变化时清除。相关完整CTest **6/6通过**（`startup-ctest-results.txt`，69.97秒）；逐组 `startup-final-{settings-apply,settings-draft,backup,backup-dialog,ui,settings}-results.txt` 为 **22／25／189／14／126／146 passed**，无失败，管理窗口另1项真实敏感卷条件skip、设置另17项既有环境或手工门控skip。翻译及占位符 **497 source / 524 catalog** 通过。

Mac真实鼠标、另一窗口激活后刷新、失败不假开、NotFound重试、待批准取消、JSON／备份及其他草稿保持均通过（`startup-ui-native-results.txt`，**7 passed / 0 skipped**，2.430秒）；系统状态由fixture提供，不将其等同实际登记。另有专属 `local.still.startup-regression` 签名测试包实际调用登记、注销和清理。最初把新应用的状态强行假定Disabled导致前置失败，`startup-registration-{,launchservices-}results.txt`保留；真实新包返回NotFound，允许该前态后 `startup-registration-attempt-results.txt` **4 passed / 0 skipped**，51毫秒。最终测试进一步核对真实bundle ID，并改成QCoreApplication＋LSUIElement无窗口后台包，`startup-background-registration-results.txt` **4 passed / 0 skipped**，54毫秒，包含实际登记、撤销和最终清理检查；未操作生产Still身份的登记。普通CTest的登记动作仍需显式 `STILL_STARTUP_REGISTRATION_TEST=1`，不默认改变系统启动项。未实际注销账户或重新登录，不声称已验证自动打开应用。

打包同时补齐：先前Still仅有linker ad-hoc签名，Info.plist未绑定、资源未seal。非Xcode开发构建现链接后完整ad-hoc签名，图标与生成Info.plist进入LINK_DEPENDS，防止仅资源更新时跳过重签；Xcode仍使用原签名流程。Still和独立测试包均通过 `codesign --verify --strict`。隔离测试包仅修改生成plist的空白后，严格签名检查如期失败（`startup-resource-before-results.txt`）；正常构建重新链接并签名后通过（`startup-resource-rebuild-results.txt`／`startup-resource-after-results.txt`），再还原原plist字节并重签核对。未改动用户原图标文件；开发ad-hoc不等同正式分发签名、公证或最终安装包验收。

界面最终8图为 `.impeccable/review/qml-startup-{disabled,approval}-{normal-light-zh,normal-dark-en,compact-light-en,compact-dark-zh}.png`，1100×760／800×620逻辑尺寸、DPR2；根代理与独立终审逐张查看，`ship`、无material fixes，仅覆盖本轮UI，记录见 `.impeccable/review/startup-design-review.md`。用户反馈原生测试造成大量exec Dock临时项后，检查确认测试进程已退出且无新增Dock固定项，仅刷新当前用户Dock；登录项API测试已去掉GUI初始化，后续必要界面验收合并串行，不再反复启动裸GUI程序。Windows、真实系统撤销批准、移动应用、重新登录、正式分发及完整辅助功能继续待补；本项保持部分完成，下一增量继续 ST-109，不进入P2／P3。

2026-11-07 ST-109 第二增量：Windows源码接入Task Scheduler 2.0，明确“任务计划程序”管理入口。只在用户显式开启时，为当前SID登记InteractiveToken／LeastPrivilege的单一Logon触发和当前程序Exec动作，不提权、不经shell、不要求密码；不迁移Run项或修改StartupApproved。传统Run项存在不能证明任务管理器允许状态，因此未将其冒充有效启用。方案依据[微软任务安全上下文](https://learn.microsoft.com/en-us/windows/win32/taskschd/security-contexts-for-running-tasks)、[任务XML schema](https://learn.microsoft.com/en-us/windows/win32/taskschd/task-scheduler-schema)和[登记API](https://learn.microsoft.com/en-us/windows/win32/api/taskschd/nf-taskschd-itaskfolder-registertaskdefinition)核对；当前没有Windows编译或运行通过记录。

`startup_win.*` 仅拥有COM／当前用户系统调用，`startup_task.*` 单一拥有XML生成和检查。Source＋Documentation标识自有任务，异属登记拒绝更新或删除；路径失效可显式重登记，更新不先删除旧任务，关闭删除自有登记。查询检查任务与触发器Enabled、双SID、交互／普通权限、唯一命令与无额外参数，并明确关闭电池及默认72小时限制。外部禁用只读显示，不自动修复；错误及正值登记警告均返回真实系统说明并重读结果，不声称系统服务异常下有事务回滚。Windows原生测试使用独立GUID＋SID＋UUID任务和 `STILL_WINDOWS_STARTUP_TEST=1` 门控，QCoreApplication无界面，尚未在Windows执行。

共享界面改为平台管理标题，Windows普通状态也可打开任务计划程序，只有失效／待批准才出现取消登记；Mac默认系统登录项入口保持。修复“本次操作返回警告但实际状态已变，错误被被动刷新清掉”的已确认RED：`startup-windows-warning-before-results.txt` **2 passed / 1 failed**。操作完成现在直接保存读回状态与本次错误，被动外部状态变化仍清旧错误；不乐观改开关，也不掩盖部分生效。Qt布局更新后的按钮交互先完成真实frame polish，保留原设置／备份／草稿隔离断言。

Mac当前构建通过（`startup-windows-release-build-results.txt`／`startup-windows-ui-polished-build-results.txt`）；相关离屏CTest **2/2通过**（`startup-windows-ctest-results.txt`，62.92秒）。公共任务模型 **39 passed / 0 skipped**，完整设置 **147 passed / 17 既有环境或手工门控 skipped**，均无失败，独立结果为 `startup-windows-final-{startup-task,settings}-results.txt`；目标UI回归 **8 passed / 0 skipped**（`startup-windows-ui-final-results.txt`）。模型使用基于官方schema的独立样例及真实本机临时文件，覆盖转义、路径别名、旧路径、所有权、额外动作／触发器、身份、权限、条件和损坏XML，不能据此宣称Windows实际XML导出通过。翻译及占位符 **507 source / 535 catalog** 通过。首次链接因Qt6.12 moc未识别含特定双引号xmlns的raw XML样例而失败；仅测试样例改成等价单引号，直接moc与最终构建恢复，不改业务解析代码掩盖失败。

当前环境已确认缺少Windows SDK、MinGW、Qt Windows与Wine，clang Windows target探针因缺 `windows.h`／C++标准库失败；Mac arm64 Qt不可替代。独立静态复核对照Microsoft及MinGW官方接口未发现确定签名／链接缺项，但不计原生通过。用户确认目前只有Mac，Windows编译、Task Scheduler真实登记／外部禁用、移动路径、重新登录与Windows字体／辅助功能均保留待验收，不继续反复请求不存在的环境。

本轮界面验证仅在Mac离屏执行；最终四图为 `.impeccable/review/offscreen/qml-startup-task-enabled-{normal-light-zh,normal-dark-en,compact-light-en,compact-dark-zh}.png`，1100×760／800×620、DPR1，合成Windows状态并使用Mac字体，不是Windows原生截图。专门快照 **6 passed / 0 skipped**（`startup-windows-layout-results.txt`），隔离完整suite遗留的无关快捷键反馈；根代理与独立终审逐张查看，`ship`无material fixes，仅覆盖此共享界面，记录见 `.impeccable/review/startup-task-design-review.md`。共享状态结构变更后，Mac原有后台目标重新构建并仅运行 `readingDoesNotRegister`，**3 passed / 0 skipped**、13毫秒（`startup-windows-mac-readonly-results.txt`）；不登记或撤销启动项。本轮未启动Mac原生GUI或更改用户系统登记。ST-109不标全项完成；当前转向ST-110的实际工作流，P2／P3不自动开发。

2026-11-07 ST-110 首增量：用户选择 OCR 文字校对作为首个实际 API 功能。通用偏好底部提供“API 文字校对设置…”，OCR 结果窗口按当前校对稿选择“API 校对选中文字…”或“API 校对全部文字…”。打开预览不发送，点击“发送校对”才提交固定校对指令与此次文字快照，不附图片、历史或其他上下文；本机系统 OCR 和“应用整理”继续无需密钥。显示发送范围、完整地址和模型，结果可编辑，明确应用后才一次可撤销替换；原始结果、选区外 CRLF／空白和 Unicode 字符保持，不回写图像或贴图选字框。独立审查发现未编辑返回值经 `toPlainText` 会改变 NBSP／换行，已改为保留服务 raw 文本，补直接应用／撤销回归。

只接一种 OpenAI 兼容 Chat Completions 非流式协议，完整 URL 不自动拼接，不预设服务或模型；远端 HTTPS＋自备密钥，本机回环 HTTP 可无密钥。客户端单次请求、不跟随重定向、不自动重试／回退，输入最多65536个UTF-16字符、响应1 MiB、总时限60秒。无效、空白、拒绝或截断结果不可应用；真实 token 用量缺失时显示未知，不估算价格。取消停止本机等待，不保证撤销服务处理或计费。协议依据 [OpenAI 官方 API 文档](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)；凭据方案参考 [Apple TN3137](https://developer.apple.com/documentation/technotes/tn3137-on-mac-keychains) 与 [Microsoft Credential](https://learn.microsoft.com/en-us/windows/win32/api/wincred/ns-wincred-credentialw)。

`api-profile.json` 独立保存 endpoint／model，不改严格版本1备份字段集；密钥、文字、结果及用量不写普通配置或日志。按完整 endpoint 的 SHA-256 隔离系统账户，地址变化清空未保存密钥，不跨服务复用；Mac 使用适配当前 ad-hoc 签名的用户登录 file-based Keychain，禁同步、无授权弹窗并保留 ACL，失败明确报错，不退回普通文件，也不宣称 ThisDeviceOnly。Windows Credential Manager 源码已接入，但原生编译／执行未验证。设置支持替换／删除当前地址密钥、固定样例连接测试及取消；密钥写入成功而 profile 保存失败时明确报告部分成功。

Mac构建通过（`api-delivery-build-results.txt`）；补齐两条既有配置预览翻译后再次构建Still（`api-catalog-build-results.txt`），bundle严格签名检查通过。当前专项为客户端 **57 passed**、profile **14 passed**、API设置 **15 passed**、OCR结果 **35 passed**、原有UI **126 passed**，均无失败／跳过，日志为 `api-client-results.txt`、`api-profile-results.txt`、`api-settings-results.txt`、`ocr-result-results.txt`、`ui-results.txt`。本地 HTTP 模拟服务验证精确 snapshot、认证、真实／未知用量、重定向拒绝、空／截断／错误响应、取消／销毁及显式重试；未变化结果明确说明无需更改，不承诺新增撤销项。受限工具沙箱不允许回环监听，需允许本地端口的执行环境，不涉及真实外网 API。Mac 后台原生凭据测试 **4 passed / 0 skipped，34ms**（`api-credentials-native-unsandboxed-results.txt`），仅用独立 UUID 账户写入、替换、读取并清理，`STILL_API_CREDENTIAL_TEST=1` 显式门控；普通测试使用独立 fixture，不读用户密钥。

完整相关回归最终 **6/6 通过，74.82秒**（`api-delivery-ctest-results.txt`），设置 **148 passed / 17 skipped**，跳过项为既有环境／手工门控。首轮曾在设置备份快照崩溃：新增API入口用例遗漏恢复原页面，触发后续双Application组合；补齐测试清理后原失败组合 **8 passed / 0 skipped，2.651秒**（`api-settings-isolation-results.txt`），完整suite通过。页面通知和Loader的诊断探针均已撤回，没有据此改动产品布局。翻译覆盖 **507 source / 609 catalog**、缺失及占位符错误均0（`api-translation-results.txt`）。最终图位于 `.impeccable/review/offscreen/ocr-api-{entry,before-send,preview}-*.png` 与 `api-settings-{empty,connected,error}-*.png`，覆盖中英、明暗、普通／480px紧凑布局，Mac字体、DPR1。根代理及独立终审查看后 UI `ship`、无material fixes，仅覆盖离屏显示，设计记录见 `.impeccable/review/api-proofreading-design-review.md`。本轮未启动原生 GUI、操作 Dock 或请求真实外部 API；真实远端自备密钥连通性／计费、原生窗口、Windows、60秒墙钟超时与完整辅助功能仍待验收，ST-110及P1总目标保持进行中，不进入P2／P3。


2026-11-07 ST-110 验收补充：新增显式门控 `STILL_API_TIMEOUT_TEST=1`，使用真实60000毫秒生产定时器及持续不返回的回环服务，不缩短时限、不替换定时器。实测 **60.350秒**触发超时，**3 passed / 0 skipped**、总60.444秒（`api-timeout-native-results.txt`），覆盖仅结束一次、无可应用结果、用量未知、连接中止、无自动重试和同一客户端显式重试成功。默认门控 **2 passed / 1 skipped**、1毫秒（`api-timeout-default-gate-results.txt`）；客户端全量快测 **57 passed / 1 skipped**（`api-timeout-client-results.txt`），新增跳过仅为此60秒慢测。断言容许默认CoarseTimer提前5%的官方边界，系统忙时可稍晚；见 [Qt定时器精度](https://doc.qt.io/qt-6/qtimer.html#accuracy-and-timer-resolution)，本轮不更改生产定时器。

同轮通过Cua实际操作具名后台测试包，运行当前产品API设置、OCR结果和API预览源码；只用隔离临时profile、唯一回环地址、空密钥和合成文字。原生sheet已确认密钥字段遮蔽、固定样例连接成功及12／8／20 tokens；选择首行后预览仅含选区，明确点击发送才增加请求，返回稿可手工编辑并应用，选区外两行和双空格保留。首轮测试宿主重新激活同一结果窗口后，Command+Z恢复原稿，原始结果页只读且API动作禁用；后续直接焦点、取消及错误复验见下文。此为独立宿主的当前对话框验收，不等同生产Still主窗口的完整接线或真实供应商。各轮共用同一具名后台bundle，未启动裸exec GUI；交接结束后宿主均已退出并清理隔离profile。

用户选择先完成Mac其他验收，真实远端服务稍后在应用中配置；不向聊天索取密钥。自动拖出未投递后，用户完成一次手工交接。Cua核对本机TextEdit中的文字精确为 `Still OCR drag sample`；接收端“撤销拖移”使文稿为空，重做恢复，保存至隔离临时文稿后再次核对文件内容。OCR源三行保持，因此本次验证的是复制拖出，不计源端Move／删除撤销分支。TextEdit与宿主均已退出，无待手工拖动现场；此前自动化失败不作为产品拖动故障。


同轮补齐API完成后的焦点回归：带普通宿主的结果窗口在应用后 `QApplication::activeWindow()` 为null，修复前 **2 passed / 1 failed**（`api-focus-before-results.txt`）。`OcrResultDialog` 现在在子窗口finished后，以自身为上下文排入零时延回调，激活校对窗口并聚焦编辑框；accepted只提交文字与状态，取消／关闭也返回校对稿。上下文销毁会取消回调，不增加全局焦点管理或重新show已关闭窗口。应用／关闭两路径 **4 passed / 0 skipped**、318毫秒（`api-focus-fixed-results.txt`）；相关OCR、UI、设置 **3/3通过，69.09秒**（`api-focus-final-ctest-results.txt`），分别 **37／126／148 passed**，设置保留17项既有环境／手工跳过。Still重新构建并严格签名检查通过。当前源码随后在同一原生隔离宿主重建复验：应用后立即Command+Z恢复原稿，无需点宿主重新激活；校对请求取消、设置连接测试取消均恢复控件，HTTP401固定错误不可应用且源稿不变。应用、取消关闭、错误关闭三条路径均直接回到校对编辑框；本进程只读Qt遥测 `api-focus-native-telemetry-results.txt`（pid90982）记录真实activeWindow／focusWidget为OcrResultDialog／OcrEditedText，不以Cua窗口选择代替焦点证据。入口接线已有QML集成回归；本轮未手工串联生产Still主窗口整流程，也不代表Windows或真实远端通过。

2026-11-07 ST-109移动补验：新增GUILESS `still_mac_startup_move`，普通运行 **3 passed / 1 skipped**、177毫秒；显式 `STILL_STARTUP_MOVE_TEST=1` 后 **4 passed / 0 skipped**、1009毫秒（`startup-move-{default,native}-results.txt`）。仅为独立UUID身份生成、签名和登记临时包，同卷实际重命名Original.app至Moved.app后，新进程查询仍为Enabled；新路径可关闭、重新登记并最终确认Disabled，再移除临时目录，根代理另行核对目录不存在。此结果说明移动不必然产生NotFound，不等同Finder／LaunchServices移动、跨卷移动、批准撤销或实际重新登录。

2026-11-07 ST-108系统面板补验：同一具名后台宿主接入当前产品 `SettingsBackupDialog`／`SettingsApply`，隔离配置、历史和热键fixture，不强制关闭原生面板。Cua实际确认NSSavePanel导出、NSOpenPanel选择完整v1样例及NSAlert恢复确认；本进程 `dontUseNativeDialogs=0`，三类Qt代理的 `WA_DontShowOnScreen=1`，见 `settings-native-panels-telemetry-results.txt`（pid92011）。样例只修改文件名模板；确认框按Return默认取消，磁盘原配置保持，随后明确Yes后持久化配置与导出原值相比只有该模板改变，见 `settings-native-panels-persistence-results.txt`。目录不迁移、配额不降低；未触碰用户配置或历史，结束后宿主退出且隔离目录已清理。此为生产对话框源码在隔离宿主中的系统路径验收，不扩大为Windows或完整生产主窗口集成验收。

2026-11-07 ST-101文件粘贴补验：用户明确允许替换当前剪贴板。具名后台隔离宿主直接调用产品 `copyImageToClipboard`，复制一张64×40合成PNG的文件引用；Cua先在独立空目录用Finder粘贴成功，再结束生产者pid92842，经Cua进程列表和精确可执行路径pgrep确认退出后，在另一空目录粘贴成功。两次文件均为210字节，独立读取逐字节等于源PNG，SHA-256一致，尺寸及合成像素校验通过，详见 `copy-file-finder-native-results.json`。宿主及隔离profile已清理，只关闭本轮创建的Finder窗口；临时源PNG按产品约定继续存在，当前剪贴板仍为该合成文件引用。此补齐Mac文件粘贴与生产者退出后粘贴证据，Windows Explorer和其他可选writer仍按实际环境验收。

#### P1 当前剩余验收核对

此表汇总本节已有源码与专项日志，不替代原完成标准；未列为通过的原生能力继续保留。

| 项目 | 这台Mac已有直接证据 | 仍需的具体验收 |
| --- | --- | --- |
| ST-101 | 快速保存、编码、原生设置；Finder真实粘贴及生产者退出后粘贴，文件字节一致 | Windows Explorer；WebP／TIFF仅在实际writer可用后接入 |
| ST-102 | 正常退出与恢复、原生双屏布局 | 真实屏幕拔除／重接；Windows |
| ST-103 | 纯文本／安全HTML贴图原生8 passed | Windows |
| ST-104 | 标注编辑及F2／取消／焦点原生补验 | Windows；PNG重开对象编辑不属于本轮最低交付 |
| ST-105 | 裁切／翻转／颜色／亮度原生专项 | Windows |
| ST-106 | 延时、宽高、比例与命名区域原生专项 | 真实桌面像素、混合DPI／拔屏、权限拒绝后重新授权；Windows |
| ST-107 | 原生校对／整理／复制、鼠标选字；真实TextEdit复制拖出及接收端撤销／重做 | Windows |
| ST-108 | 配置恢复与单项默认专项；当前产品对话框的系统NSOpenPanel／NSSavePanel／NSAlert及取消／确认落盘 | Windows |
| ST-109 | 隔离身份真实登记／撤销；同卷文件系统移动后的新路径启停及注销清理 | 实际重新登录、外部批准撤销、Finder／跨卷移动；Windows原生编译／调用 |
| ST-110 | 回环协议、真实钥匙串、真实超时；独立宿主原生设置／预览／应用／立即撤销／取消／HTTP错误及直接焦点 | 用户实际远端服务已选择稍后配置；Windows |

### P2：增强高频使用深度

| 任务 | 对应差距与建议顺序 | 完成标准 |
| --- | --- | --- |
| ST-201 | F02/F04，先子元素吸附，再圆角/折线区域 | 可在窗口和子元素间切换；目标应用不提供可访问元素时保留窗口/手动框选；异形输出透明边缘在支持的格式下正确。 |
| ST-202 | F05/F07，光标、辅助线、多格式取色和坐标 | 光标与截图像素位置一致，格式切换及坐标含义明确；不混淆逻辑坐标和图像像素。 |
| ST-203 | F10，折线 → 模糊/橡皮擦 → 聚光灯/水印/放大镜标注 | 复用 P1 对象编辑能力；每个工具有撤销、导出和缩放测试；再补箭头评论文字和借助 OCR 框选择标注范围，工具记忆只记录真实可用选项。 |
| ST-204 | F21/F22，鼠标穿透、锁定、缩略图、分组/多选 | 穿透和锁定都保留可找回的退出入口；缩略图不损失原图；组内显隐/恢复不会误操作其他贴图；先完成 ST-102。 |
| ST-205 | F27，暂停/恢复与手动裁剪 → 横向 → 自动滚动 | 先稳定现有纵向和预览，再补手动裁剪、横向、自动滚动和反向滚动自动裁剪；处理均在本机，自动滚动可停止且不抢用户输入，失配保留可靠结果。超长图必须先验证内存/导出策略，不直接调大现有限额。 |
| ST-206 | F14，二维码及工业条码 | 先在本机支持一个/多个二维码，再增加常见一维码和工业条码；结果可复制，链接不自动打开。先评估平台接口，再按实际缺口选择依赖。 |
| ST-207 | F06，跨屏选区和跨屏长截图 | 建立屏幕坐标和像素映射样例；不同缩放、负坐标和屏幕断开时结果明确；先通过 ST-001 的单屏及多屏基线。 |
| ST-208 | F20/F24，图像文件/颜色贴图、内容拖出、打印 | 文件贴图先明确是预览还是文件引用，缺失文件有反馈；颜色不假装图片 OCR；拖出和打印使用当前可见处理结果。LaTeX 留到 ST-303。 |
| ST-209 | F32/F33，局部快捷键和鼠标映射 → 托盘/工具栏定制 | 先覆盖现有动作，不先搭通用脚本系统；冲突/忽略程序设置可解释；鼠标映射不破坏选字/拖动；按钮隐藏后仍有恢复入口。 |
| ST-210 | F12/F36，多语言 OCR 与语音能力 | 先扩展系统可用语言和本地语音，再按需要支持本地模型或用户配置的 API；分别验收多语言识别、文本位置框、朗读与音频导出，不把语言菜单当作模型已可用。 |
| ST-211 | F29/F37，编码预览及 PDF 输出 | 在本机编码并预览实际文件大小；再增加 PDF 页边距和分页。普通图像 PDF 输出与 ST-305 的可搜索、结构化文档分别验收。 |

### P3：高级能力逐步交付

| 任务 | 范围 | 进入实现前的判断与验收方向 |
| --- | --- | --- |
| ST-301 | F28，屏幕录制 | 优先使用系统捕获与本机编码，评估 CPU/GPU、内存和磁盘预算。先交付区域 MP4、开始/暂停/停止/取消，再依次补 GIF/WebP、麦克风/系统音频、键鼠演示、摄像头画中画和录制后剪辑。录制生命周期独立于截图/拼接会话。 |
| ST-302 | F15，表格识别 | 可先用用户配置的视觉模型 API 交付，再补本地推理；识别单元格、合并关系并允许校正，CSV 优先，再增加 XLSX。纯 OCR 按空格分列不算完成。 |
| ST-303 | F16/F20，公式识别与 LaTeX 贴图 | 公式识别支持本地模型或自备 API Key；公式渲染在本机完成。分别验证识别正确性、LaTeX 编辑和渲染；增加本地模型前核对硬件成本、许可与体积。 |
| ST-304 | F11，自动隐私马赛克与智能擦除 | 先实现本地可校正的隐私候选框，再交付图像修复；修复可选择本地模型或用户指定的图像 API。结果可预览、撤销并人工校正，不宣称检测不会遗漏。 |
| ST-305 | F17，文档识别与结构化导出 | 支持本地版面/视觉模型或用户配置的 API；文字、表格、公式和阅读顺序可校正。先 Markdown，再逐步补 Word、可搜索 PDF 和 LaTeX；图表与排版保真分别验收。 |
| ST-306 | F18，文字/图片翻译 | 纳入正式迭代。先支持用户自备 API Key 的文本翻译及图像到文本翻译，再补本地翻译模型和图像回填翻译；保留原文、目标语言与结果编辑。按 ST-110 明确计算位置和发送范围，不自动上传。 |
| ST-307 | F25，实时窗口贴图 | 使用系统窗口捕获在本机持续刷新，先单窗口再多窗口；分别验证遮挡、最小化、原窗口关闭及多屏行为，并明确平台限制。 |
| ST-308 | F35，用户自有配置同步 | 先支持用户自选同步文件夹，再按需要接入用户自有存储服务；不要求 Still 账号。配置合并、冲突恢复和离线行为明确，密钥、机器路径和剪贴板/截图内容不随普通配置默认同步。 |

## 保留现有优势与边界

- 剪贴板持久化、搜索/固定、纯文本重放、目标窗口失效保护、历史配额和目录迁移已经实现，后续迭代必须持续验证，不能在补截图功能时退化。
- 图片旋转、缩放、透明度、贴图文字选择、手动纵向长截图、基础标注、中文/英文和浅深主题已有实现，补齐差距时复用其当前职责，避免平行重写。
- 不引入会员、订阅或付费解锁。配置同步通过用户自己的存储完成，不要求 Still 账号；使用统计不作为高级能力的前提，自动更新另按分发需求安排。
- 代码和资源独立实现；本地模型与新依赖按具体功能评估许可、分发体积和硬件要求，不提前引入。

## 每次更新本计划的要求

任务完成时附源码入口、验证环境、结果和剩余限制，再将相应差距改为“已有”。只有菜单或配置字段、编译成功、离屏截图中的任意一种，都不能单独代替原生功能验收。新增功能优先完成一个可用流程，再扩展参数和高级选项。
