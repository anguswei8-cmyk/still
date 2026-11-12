# 拾影 / Still Qt 交接

> 旧设计工具的截图、审查记录及局部说明已于 2026-11-12 清理；下文保留当时的验收记录。

更新日期：2026-11-07。当前主实现为 C++17 / Qt >= 6.8，版本 0.2.0。主窗口、侧栏、剪贴板与五类设置使用 Qt Quick/QML；截图选区、编辑器、贴图及原生辅助对话框保留 Widgets。

## 当前迭代

后续任务以 `ITERATION-PLAN.md` 为唯一列表，当前目标仅 P1；P2／P3 保留为路线，不自动开发。用户目前只有这台 Mac，Windows 原生验收继续保留。ST-101 已接通快速保存及按设置复制临时图像文件：Ctrl/Cmd+Shift+S 直接保存，Ctrl/Cmd+S 保留对话框。`storage/image_export.*` 负责模板展开、保存选项及无覆盖发布；`store` 继续负责实际编码和原子写入。模板、PNG/JPEG 与参数由 SettingsDraft 在配置边界校验；默认 `still_{date}_{time}_{ms}`、PNG、JPEG 95、PNG 压缩 50。修改保存设置会更新现有编辑器和贴图；成功信号回到 Application 处理历史与“保存后复制”。快速保存失败保留编辑器/贴图，选区与长截图失败转入编辑器。回归入口为 `image_export_test.cpp`、`settings_draft_test.cpp`、`ui_test.cpp` 与 `settings_test.cpp` 的 savePreferences 场景。其他格式待有实际编码器的构建验证；本机 writer 列表没有 WebP/TIFF。

`clipboard/image_copy.*` 统一图片复制；`save.copyAsFile` 默认 false，true 时在系统临时目录创建独立 `Still-clipboard-*` 文件夹，复用 exportImage 编码后再交付文件 URL。文件模式只交付文件引用，普通模式仍交付图片；OCR 文字与 ClipboardController 原格式重放不变。复用 restoreClipboard 的 Windows Copy DropEffect，避免变成移动操作。编码失败不替换原剪贴板；保存成功但后续复制失败时仅报告复制失败，不撤销已保存图片。临时文件不随剪贴板/窗口/应用销毁而删除，由系统临时目录清理，当前不另设清理配额。文件历史仍只记录引用，系统清理后沿用已有“文件已移动或删除”反馈；不要按过期时间盲删仍被历史/其他应用引用的临时文件。

ST-101原生补验已通过：用户明确允许替换当前剪贴板，具名隔离宿主直接调用生产 `copyImageToClipboard` 复制64×40合成PNG，Finder在生产者运行时及确认退出后各粘贴一次；两份文件字节均与原PNG一致。证据见 `cmake-build-debug/copy-file-finder-native-results.json`。本轮宿主、隔离profile及Finder测试窗口已清理，原临时PNG按产品约定保留以维持当前剪贴板文件引用；不恢复用户已允许替换的旧内容。Windows Explorer仍待原生验收。

下文保留早期 Windows 验证记录，不代表当前 Mac 原生完整验收；最新测试状态见 `ITERATION-PLAN.md`。

ST-102 新增 `imaging/pin_state.h` 与 `storage/pin_session.*`：PinState 保存图片和窗口布局；PinSession 管理当前运行的关闭栈及独立的会话索引/PNG。关闭栈最多20张/256 MiB，至少保留最近一张。读取在后台执行，排除当前已显示 ID、按剩余贴图容量读取，重复恢复不复制窗口；缺图、索引无效或容量不足时保留旧会话，不用部分成功结果覆盖它。Pin 只负责导出状态与应用布局，屏幕缺失时约束到当前可用区域。

正常退出前 Application 的 qApp Quit 事件过滤器先捕获未关闭窗口；Qt 随后产生的 Close 不能进入“最近关闭”栈，否则退出会误保存为空。析构等待在途读取，再在工作线程写整组新 PNG、原子提交 `PinSessions/session.json`，成功后只清理旧索引引用的目录。直接销毁控制器也保存当前贴图；无贴图活动时保留旧会话。该增量不提供崩溃恢复；保存失败保留旧文件并输出错误。`pin.restoreOnStartup` 默认false，正常退出时的隐藏贴图仍属于未关闭集合，恢复时显示。测试包含真实 QApplication.quit 子进程，不能仅以直接析构测试代替退出行为。

Mac 首次显示窗口的原生回调可能覆盖先前的恢复坐标。Pin 先指定目标屏幕并设置可见范围内布局，再排队应用一次布局；不要改为固定延时或移除位置断言。`restoredPinUsesTheSavedScreen` 在两块真实屏幕上各重复15次，修复前失败、修复后通过；调试日志已从源码移除。最终非 integration CTest12/12通过；`pin-layout-final-results.txt` 为6 passed，覆盖双屏、关闭/重启恢复及真实退出。菜单独立审阅见 `compact-menu-design-review.md`，`ship` 仅限界面组织和所示视口。

按用户最新要求，贴图右键收为5个主入口，其他动作归“更多操作”；菜单栏图标收为6个主入口，贴图管理和额外截图动作进入二级菜单。所有回调与既有快捷键保留；后续功能优先放到合适分组，避免继续向主菜单堆叠。

ST-103 已从首个纯文本增量扩展到 `text/html` 文字排版。`PinWindow` 统一窗口身份、布局、关闭和公共请求，保留 Cocoa 延后恢复位置；图片 `Pin` 继续独立负责 OCR/图像操作，`TextPin` 用只读 QTextEdit 负责文字选择、复制和滚动，双击选词而非关闭，滚轮不缩放。纯文本仍通过 UTF-16 边界映射截取原文，保留 QTextDocument 规范化前的 CRLF 和空白。存在 HTML 时显示 Qt 支持的标题、粗斜体、列表、表格、预格式文本和内联 CSS 文字格式；富文本复制仅交付可见纯文本，选中时复制选区，否则复制全文，不携带 HTML，也不复制原始后备文本。RTF-only 仍按纯文本展示，不保证完整网页还原。

历史的 text/rich/link 复用原贴图入口，`ClipAction::PinText` 交付 payload.text 与已有 `text/html`；直接贴图也自动采用 HTML，仍优先图片并排除文件 URL。原文与 HTML 合计 32 MiB UTF-8 的限额在两个入口与存储边界校验。创建贴图不改写系统剪贴板或归档中的原始 MIME；菜单仍仅复制、全选、更多、关闭，恢复/批量操作留在更多中，未新增主菜单入口。

`imaging/html_document.*` 的 SafeHtmlDocument 是共享的 QtGui 文字排版边界，供富文本贴图及 HTML-only 纯文本后备提取使用。必须在解析前替换 Qt 默认图片处理器：它可能绕过 loadResource 直接读文件。loadResource 返回有效空值，同时阻止实例/全局资源提供方；解析后删除图片对象、链接动作及字符和块级富文本 tooltip 元数据，避免 tooltip 经独立文档加载资源。当前不加载图片、数据 URL、本地/qrc/远程资源或外部样式表，不使用浏览器/WebEngine/JavaScript，链接不会打开。

PinState 保存 text 与 html；会话 v3 按 kind 写独立 PNG/TXT，富文本同时写原文后备 TXT 与 HTML，严格验证 UTF-8 和路径，兼容读取 v1 图片及 v2 纯文本会话。整组新内容写完后才原子提交索引，失败保留旧会话；任一富文本内容文件缺失、编码无效或超限时跳过整张贴图，不降级为纯文本，恢复不完整时退出仍保留原会话。关闭栈和会话恢复均保留 HTML。旧版程序拒绝 v3 并保留索引，不能静默丢弃富文本格式。

HTML 增量已加入资源隔离、可见文本复制、混合会话与失败保护回归；首段为空时遗留块级 tooltip 的回归已确认修复前失败。本增量 Mac 全量构建及非 integration CTest 12/12 通过：UI46、设置67/12环境跳过、会话19、归档26、派发17 passed。原生入口、混合恢复、真实退出与12张中英深浅/正常紧凑/选择/滚动/菜单截图8 passed；日志见 ITERATION-PLAN.md，Windows 与完整辅助功能仍待验收。

ST-104 首轮为当前编辑文档内的标注对象编辑。`ImageDocument` 保留 11 类标注的有序对象列表，以索引和单次变更的 before/after 记录新增、替换、删除；撤销重做恢复内容与原层序，新的有效修改丢弃 redo，无变化不丢弃 redo。原图不改写，`render()` 从原图按对象顺序合成。序号自动取当前未使用的最小正整数，删除中间序号后补空位，不按对象数量生成重复标签。

`Canvas` 管理选择与手势，工具栏新增 V“选择标注”和 F2“修改标注文字”，原有 11 个绘制工具及快捷键保持；不新增右键菜单。命中从最上层向下，按实际绘制形状检测，空心矩形／圆角矩形／椭圆仅命中轮廓，容差为 5 个逻辑像素。拖动预览从原图按全部对象的原层序重绘，只替换被移动对象，避免残影及马赛克重复采样；释放才提交一次替换，可一步撤销。方向键每次移动 1 个源图片像素，Shift 为 10 个；整体平移保持点间关系，裁切后仍编辑原对象，不把画外锚点逐点裁成新形状。

选中后颜色、线宽控件显示当前对象值并直接修改该对象；马赛克和马赛克笔禁用颜色，矩形马赛克同时禁用不适用的线宽。文字／序号选中后可 F2 或双击改字，对话框带入原文；取消不修改，确认空文字删除对象。画布 Delete/Backspace 删除选中对象；选择模式未选中时不操作，绘制模式未选中时沿用原撤销行为。线宽输入框中的 Delete 只编辑数字。Esc 优先取消绘制、拖动或选择，无交互时关闭编辑器；切工具取消未完成手势。撤销重做后清除选择，未提交手势的撤销先取消手势。

编辑器复制、保存、快速保存、OCR、贴图及“应用并关闭”统一使用已提交的 `ImageDocument::render()`，不输出选择框或未完成手势。对象与变更记录仅属于当前编辑文档，不写入 PNG/JPEG、截图历史、PinState 或 PinSession；从历史／贴图再次编辑时以扁平图片为底图，不能恢复旧标注，关闭编辑器或重启也不保留对象。不要把贴图会话恢复等同于可编辑标注恢复。

本增量回归入口为 `core_test.cpp` 的变更记录、真实形状命中、序号补位和马赛克层序，`ui_test.cpp` 的选择／移动／样式／改字／删除／取消及输出场景，以及 `settings_test.cpp` 的原生对象编辑、改字后继续使用和中英深浅正常／紧凑截图。ST-104 构建、完整回归和原生验收状态见 `ITERATION-PLAN.md`；上述 ST-103 通过数不作为本增量验证结果。

ST-105 在同一 `ImageDocument` 变更记录中加入裁切、水平和垂直翻转、灰度、反色及亮度。`original_` 保留原图，`currentFrame_` 记录当前源区域与镜像方向，每个标注保留创建时的画幅；合成按原层序在各自画幅绘制，再投影到当前画幅。这样裁切不会改变旧文字换行或马赛克采样，翻转后的旧对象保持镜像，新标注则按当前画幅正向绘制。命中、选择框与移动使用当前画幅坐标，旧对象仍能修改；裁掉的部分可通过撤销恢复。

编辑器 C 进入裁切，拖动框选保留区域；Enter、再次按 C 或裁切主按钮应用一次变更，Esc 或切换工具丢弃未提交区域。裁切按钮下拉按几何／颜色分组提供翻转、灰度、反色和亮度；沿用图片贴图 5、文字贴图 4、菜单栏图标 6 个主入口的组织，不增加右键菜单。复制、保存、OCR、贴图和“应用并关闭”仍统一输出已提交合成图，不带裁切区域框或亮度预览。

灰度／反色记录当时源区域与创建顺序，在原图坐标中只变换 RGB，保留 alpha。渲染合并标注与效果的创建顺序，同画幅的标注批处理不能跨越颜色处理；旧对象修改仍在原效果下，删除／恢复不改变效果层序，后来新增的标注保留所选颜色。效果与标注共用撤销历史，不缓存整图快照，也不将标注扁平化。

亮度复用 `EffectChange::amount` 与同一 RGB kernel：-100 至 100 映射为 `qRound(amount * 255 / 100.0)` 的通道偏移，RGB 饱和截断到 0–255，alpha 不变。`previewBrightness()` 每次从已提交结果生成预览，不累积上次值；Canvas 只持临时数值用于显示，不改文档或输出。QInputDialog 确认非零才提交一次；取消与 0 保留选中、裁切、redo 和旧 OCR，真正提交才取消旧 OCR。弹窗内控件局部使用 Fusion 遵循应用调色板，修复原生 Cocoa 深色取消按钮与步进箭头难辨；样式对象由弹窗持有，不改全局控件样式。

有效文档修改及撤销重做更新 revision，编辑器取消旧 OCR 请求并提示重新识别；应用到原贴图继续使用 `Pin::setImage()`，清除旧识字框和文字选择并重新识别。标注作者画幅、裁切／翻转／颜色处理状态与撤销记录仅在当前文档中保留，导出、历史及会话仍是扁平图片，关闭或重启不恢复这些编辑状态。本增量测试入口为 `core_test.cpp` 的逐像素变换、作者画幅和变换后对象编辑，`ui_test.cpp` 的裁切交互、翻转、效果、输出及 OCR 失效，以及 `settings_test.cpp` 的原生变换与截图；验证状态以 `ITERATION-PLAN.md` 为准。用户解锁后 F2 文字弹窗及裁切 C／Enter／Esc 键盘流程已分别在原生环境通过，详见计划中的独立日志，不代表完整辅助功能验收。

ST-106 首增量提供延时截图。`capture/capture_delay.*` 独立拥有非模态工具窗口、1–60 秒输入、`QDeadlineTimer` 和刷新 `QTimer`；计时器只刷新显示，按真实单调截止时间结束。`done()` 同步停止计时器后再交给 QDialog 关闭／发信号，防止取消后 deferred delete 前又超时。局部 Fusion 延续亮度弹窗的调色板处理，动态文案随中英文设置更新。

`Application` 只持一个 `QPointer<CaptureDelay>`：重复入口激活原窗口，不重置等待；finished 先清指针再在 Accepted 时调用现有 capture。普通截图先取消等待，App 析构显式删除窗口以停止其计时器。倒计时期间不调用 capture、不隐藏现有窗口或采集帧；到期复用权限检查、隐藏／等待合成器、逐屏冻结、Overlay 和输出路径。倒计时窗口先隐藏，不进入截图后的窗口恢复列表。Mac 需保留 `WA_MacAlwaysShowToolWindow`：单靠 Tool／置顶标记仍会在应用失活时隐藏，原生属性回归已保留修复前失败。

普通截图与滚动帧采集复用平台边界 `captureScreen(QScreen*)`，生产仍调用原 QScreen::grabWindow；设置测试在同一边界提供合成帧和计数，以验证取消／到期／普通截图替代且不读取真实桌面。独立 `capture_delay_test.cpp` 验证时间、取消和语言切换，应用接线及窗口自身截图在 `settings_test.cpp`。区域复用、数字宽高、固定比例与区域预设见下文，验证范围与限制以 `ITERATION-PLAN.md` 为准。

Mac 失活验收注册为独立且串行的 integration 测试 `still_mac_delay`。`--delay-activation-target` 只启动一个合成目标窗口，通过父进程 yield／目标自身 activate 完成实际应用切换，再断言目标激活、当前应用失活及倒计时仍可见；按 [AppKit 协作激活机制](https://developer.apple.com/documentation/appkit/passing-control-from-one-app-to-another-with-cooperative-activation) 驱动，不再依赖手动 deactivate。该 helper 只用于测试，不启动生产控制器、不读取屏幕或剪贴板，退出时清理子进程。旧测试直接调用 deactivate 的批次失败保留为诊断，不计通过；这项可见性验收不等于真实菜单截图像素／系统权限已验收。

`still_mac_pin_activation` 复用同一测试子进程，验证图片／纯文本／HTML 贴图在新建、切换置顶及恢复非置顶布局后实际失活仍保持 Qt 与原生可见，共9个窗口。原生产策略已通过此项，未为贴图追加倒计时的 Mac 属性修复。测试先复用打开剪贴板入口建立本应用前台条件；锁屏等原因导致15秒内仍无法激活时明确跳过，不能把此前前置条件失败视为贴图缺陷，也不能将跳过计作原生验收。最终 `mac-pin-activation-results.txt` 为3 passed／0 skipped，详细限制见计划；该用例不验证叠放像素，显式隐藏偏好及已有关闭确认行为保持。

ST-106 区域复用由 `Application` 单独持有源屏运行期 `QPointer<QScreen>` 与 `RegionPreset` 快照，其中本屏逻辑 `QRectF` 只有这一份业务记录。冻结帧同时携带源屏指针与 `screenInfo` 身份值，避免提示期间拔屏后依赖已失效指针重新读身份。Overlay 的 accepted 原样交付精确本屏局部 `QRectF`，App 直接记录；仅传给贴图／滚动等现有整数区域入口时才加冻结帧原点并 `toAlignedRect()`。不能先转全局再减原点：负坐标屏幕的浮点平移可能吞掉像素边界余量，导致重复裁切多出一列。菜单“重复上次区域”首次禁用，accepted 或成功调用命名预设后启用；普通取消、整屏和打开历史不覆盖该运行期记录。跨重启复用只来自下述用户命名预设。

重复时仍重新冻结当前屏幕，按源屏身份匹配新帧，用实际帧像素尺寸与逻辑尺寸调用 `cropFrame`，不另乘 DPR，也不复用旧像素。源屏不可用或本屏区域不能被新帧完整容纳时提示并继续普通框选，不换主屏或裁小旧区域。`deliverCapture` 按值持有图像／整数区域／动作后关闭 Overlay，再排队沿用原 scroll 或 finish／route 流程，避免读取已释放的冻结帧。新增设置回归用合成帧控制像素与分辨率上限；布局缩小仅由旧区域状态模拟，真实热插拔与混合 DPI 桌面的验收仍需另记。

区域失效提示在 Mac 采用真实 NSAlert，不能用 Qt 定时器加 activeModalWidget 假设自动关闭，也不能为截图替换生产对话框。原生测试用 `NSTimer` 的 Common／ModalPanel 模式驱动，确认系统正文后渲染自有内容视图并关闭；定时器和引用按作用域清理，超时明确失败。按 [AppKit 的视图缓存方法](https://developer.apple.com/documentation/appkit/nsview/cachedisplay%28in%3Ato%3A%29) 生成的透明 chrome 以当前系统窗口颜色铺底，只供内容复核，不是系统合成器／玻璃后景截图。非 Cocoa 仍按 Qt 自身窗口抓图；macOS 的消息框标题由系统忽略，正文与后续重选保持断言。旧挂起记录仅作诊断，完整最终结果以计划列出的日志为准。

数字宽高仍由 `Overlay::selection_` 单独拥有几何状态，两个 SpinBox 只是最终图像像素尺寸的输入／显示，自由比例范围为1到当前冻结帧宽高。自由比例修改一维保留另一维，必要时只平移以容纳指定尺寸；用 `logicalRect` 对整数像素边界作逆映射，校正已复现的浮点重建误差，不改变 `pixelRect` 的原有向外取整规则。模型刷新时阻断控件信号，输入期间关闭逐字提交；输出前 `interpretText()` 提交尚未失焦的数值。

工具条分尺寸行与原操作行，原32px按钮／20px图标保持，滚动截图的42px操作条保持。画布开始手势前先提交输入焦点，工具条禁止鼠标事件向画布穿透；输入框 Return 只提交数值，Esc 取消，画布快捷键保留。copy／scroll 新建框选仍默认松手即完成，按住 Shift 松手进入精调，继续移动／改已有选区不会提前输出。3DIP点击吸附阈值只用于新框选，不能清除已通过数字框设定的1px选区。

两数字框仅局部设置步进按钮宽18px，不换控件样式。Cocoa 默认14px宽度在 QSS 回退绘制的内缩后不够画出箭头，已用真实 DPR 1／2 窗口和箭头像素断言保留修复前失败及修复后通过。共享浅色 SpinBox 的选中文字使用深色前景，避免白字落在浅灰选区上；未改全局样式。设置测试先通过普通打开剪贴板入口建立原生应用激活条件，再断言输入焦点；只增加测试前置条件，不给生产流程加激活补丁。

`CaptureAspectRatio` 仅持有当前 Overlay 的比例配置，默认自由；固定选项为1:1、4:3、16:9、3:4、9:16，只提供当前帧能容纳最小整数单位的项。不新增持久配置或另一份几何状态。`ratioSize` 按所改宽／高就近取共同整数倍率并限制整幅上限，SpinBox 的范围与步长同步该倍率单位；切回自由恢复1px步长和全幅范围，保留当前选区。

固定比例的鼠标框选、八手柄、内部移动、窗口吸附和键盘调整都先在整数像素矩形上处理，再用 `logicalRect` 写回唯一选区。四角保留反角，四边保留反边中点，跨过反边可翻转；鼠标缩放受锚点可容纳的倍率限制，跨侧连最小单位也不能容纳时保留上一有效选区。数字调整和移动则夹住位置，不裁小尺寸。方向键增减一个比例单位，Shift为十个，Alt按输出像素平移，Shift+Alt为十个像素，不因缩帧取整而停住。`hoverSelection` 为绘制和点击吸附提供同一最终比例区域，`logicalSelection` 共用夹边与逆映射，不改变原始窗口检测矩形。比例框焦点期间不触发截图快捷键；保持标准 QComboBox 实际使用的 Qt 下拉，不为测试替换控件或弹层。

比例框沿用工具条深浅 QSS，局部前景／背景／边框使深色箭头可辨；不换全局样式或使用 Fusion。下拉最小宽度用实际文字尺寸交给 `QStyleOptionMenuItem`／`CT_MenuItem` 计算：Mac 菜单样式只在调用者提供的内容宽度上增加图标／勾选留白，不能只用未包含文字的委托尺寸提示，否则“自由比例”／“Free ratio”会截断。测试另外检查文字宽度下界和委托宽度，并实际抓取弹层；箭头像素按与背景至少3:1对比度判断，允许离屏小箭头的正常抗锯齿。

本机 Qt 6.12 的比例下拉实际走 QWidget／Qt 分支，不是 NSMenu，相关测试只保留实际 Qt 弹层的显示、语言、宽度、抓图及 Esc 关闭；前轮区域失效提示的 NSAlert 驱动另行保留。Mac 非编辑型 QComboBox 默认 TabFocus，是否能用 Tab 到达由系统全键盘导航偏好决定。测试遵循实际偏好：开启时保留真实 Tab 断言，关闭时确认跳过，再显式进入控件验证其键盘事件；不修改生产焦点策略或用户系统设置。

命名区域预设由 `storage/region_presets.*` 负责数据契约、完整读取、写前复验和 `writeJson` 原子提交，文件为数据根目录的 `region-presets.json`，版本1，包含 `presets[{name,screen:{name,manufacturer,model,serialNumber},region:[x,y,w,h]}]`。名称 trim 后非空且最多80个UTF-16单元，区分大小写并要求唯一；坐标有限、非负，尺寸为正，保留 double。缺失文件为空表，不可读、损坏、未知版本或任一无效记录均拒绝整表加载／覆盖；不部分恢复后再保存。只保存身份与几何，不保存像素、完成动作或比例配置。文件路径在 App 构造时固定，不跟随历史／剪贴板的自选目录迁移，`dataDirectory` 的品牌兼容检测包含此文件。

`matchingPresetScreen` 对全部当前屏幕唯一匹配；有序列号时要求序列号、厂商与型号一致，可忽略连接名变化；无序列号时要求名称非空且名称／厂商／型号完全一致。身份不明、缺失或歧义不自动替换主屏，匹配后还需存在实际冻结帧并能完整容纳旧区域，否则沿用提示和普通框选。`beginCapture` 复用原权限、延时取消、冻结、缩帧与输出；成功预设调用经 `rememberCaptureRegion` 更新 Repeat 和打开的管理窗口快照，不把旧图像当新截图。

`capture/capture_presets.*` 只拥有管理窗口、已加载列表、只读上次区域视图和文件操作反馈，不依赖 App 或真实 QScreen。保存／删除先写候选表，成功才提交内存；重名拒绝，删除默认No确认。窗口复用局部 Fusion 与 MacAlwaysShowToolWindow，关闭不保存隐式草稿；App 的 finished 回调立即清指针，避免 deferred delete 前重开复用待删窗口。截取前窗口先 hide 再发送预设值，避免被截图恢复流程抬回输出前方。列表 Enter／小键盘 Enter 以仅列表生效的 QShortcut 触发截取，复用按钮启用状态；名称 Enter 保存，焦点按钮 Enter 执行该按钮，双击沿用 itemActivated。空列表 Enter 不会落到默认保存。错误原因先翻译，磁盘错误另附原路径，重名错误不附文件细节；文件已被外部破坏时失败也保留旧列表、输入和路径反馈。

ST-107 的共享 `imaging/ocr_result_dialog.*` 独立负责结果快照、校对稿、文本整理及复制。截图 `Application::route(ocr)` 和编辑器识别成功后仍先原样复制，再各自新开非模态结果窗口；错误／空结果不覆盖剪贴板。App 销毁会关闭其结果窗口，编辑器窗口由父对象拥有。图片贴图只在“更多操作 → 校对识别文字…”按当前 `ocr_.text` 建立快照，后台自动识别不弹窗，校对不重跑识别、不回写 OCR 的文本范围或几何；后续图像变化也不更新已打开的快照。

校对／原始结果双页使用纯文本 `QPlainTextEdit`，不自动折行，便于保留代码与表格布局；原文只读，校对稿可编辑。换行提供保留／转空格／移除，水平空白提供保留／连续合并／移除，两个选项默认保留，点击“应用整理”才处理当前稿；不自动 trim。整理与还原原文各为一次 Qt 编辑块，可撤销。Qt 会规范化显示中的换行和 NBSP，局部 `ResultTextEdit` 把原始、整理及还原的 raw 文本快照绑定到既有文档撤销位置，普通编辑使用文档 raw 字符，避免以“显示相等”错误复用旧原文。新编辑分支会移除失效的 redo 快照，不另建编辑或撤销框架。

复制按钮按当前页的选区或全文交付；全文及全选复制保留该状态的 raw 文本，部分选区使用文档字符并将内部段落分隔转 LF。小型文本控件只重载 `createMimeDataFromSelection`，让按钮、快捷键、上下文菜单和系统拖出共享 `text/plain`，不附 HTML。拖出沿用 Qt 原生行为：编辑稿可移动／复制，移动可撤销，原文只读只能复制。结果和整理选项仅保留在当前窗口，关闭不保存校对稿；动态语言／主题切换不改内容、选区、选项或撤销历史。验证入口为独立 `ocr_result_dialog_test.cpp`、现有 `ui_test.cpp` 与 `settings_test.cpp`，范围和实测结果见迭代计划。

ST-108 首增量由 `settings/settings_backup.*` 负责已支持偏好的版本化备份，`SettingsBackupDialog` 负责文件选择、差异预览、目录选择和确认；通用设置底部只有一个“配置备份与恢复…”入口，不增加托盘菜单。文件为 `{format:"still-settings",version:1,settings:{...}}`，只包含当前支持的完整字段：复用 `SettingsDraft::rows` 的字段元信息，并包含实际生效的 `capture.maxEdge`、`pin.confirmDoubleClickClose/opacity`、`scroll.maxHeightPx/maxFrames`。未知字段、内部迁移标志和未来凭据不导出，导入时原配置中的未知字段继续保留。完整校验类型、范围、枚举、模板、单组合快捷键及重复快捷键，任一无效字段、缺失字段或未知版本均拒绝整表；不能用 `normalizeSettings` 自动修复外部文件后当作成功导入。

目录随备份保留作元信息，但默认导入候选复用当前 save/history/clipboard 的目录；勾选“同时恢复目录”才使用备份路径，并先验证本机绝对路径。全部默认也保留本机目录；导入和恢复前明确确认替换未保存输入，降低配额可能按现有规则清理旧历史。两者调用 `SettingsApply::applyBackup`：忙状态拒绝且不发出会干扰旧请求的 finished；快捷键任一预留失败即全部释放、不迁移、不保存；成功预留后与普通 apply 共用 `applyCandidate` 的 History→Clipboard→QSaveFile 提交检查点。旧自动保存仍允许冲突快捷键保留原值、其余字段保存。直接互换两个当前已占用快捷键仍按冲突整次拒绝，不先释放旧键破坏回滚。

默认快捷键表已从 App 的静态函数移到 `settings_apply` 统一复用，恢复默认不会误用 `normalizeSettings({})` 将全部快捷键清空。管理窗口由 MainWindow 用模态 exec 保护批量配置操作；导入／恢复成功再调用 `SettingsDraft::reloadSaved` 同步 form／accepted／submitted，避免旧未完成输入重新覆盖新配置，保留完成回调中的警告。窗口保留原生文件选择能力及局部 Fusion，早期独立测试切换 Qt 文件面板驱动，其本身不证明系统面板路径；后续原生补验见下文。备份字段集变更时须处理版本兼容，不能沿用版本1却静默改变完整字段要求。

导出复用原子写入，并拒绝当前配置文件的直接路径、既存符号链接别名及不区分大小写卷上的同目录大小写别名。首次配置尚未落盘时没有 canonical leaf，Mac 只读查询既存父目录的 `pathconf(_PC_CASE_SENSITIVE)`；若无法判断则明确拒写，不把所有 Mac 卷假定为不区分大小写。备份只包含偏好，不打包截图、剪贴板内容、贴图会话或区域预设文件；原生范围与结果以迭代计划为准。

ST-108系统面板已在当前源码隔离宿主实测：Cua确认原生save-panel／open-panel／alert，宿主未禁用native dialogs，Qt代理均为 `WA_DontShowOnScreen=1`。导出完整版本1备份，再选择只改模板的完整样例；Return触发默认“否”时磁盘不变，明确点“是”后与原导出逐字段比较，仅 `save.nameTemplate` 变为 `native-import_{date}`。证据为 `cmake-build-debug/settings-native-panels-{telemetry,persistence}-results.txt`，本轮宿主和隔离profile已清理。QML入口接线已有integration回归，原生补验未手工串联生产主窗口整流程；Windows系统面板仍待验收。

ST-108 第二增量在 `SettingRow.qml` 的真实字段行预留36px恢复按钮位置，已更改项悬停或键盘聚焦时才显示，Tab／Space可用，不增加菜单入口。`SettingsDraft::rows` 公开实际默认值，快捷键复用 `defaultHotkeys`；内部 `fields` 和 `normalizeSettings` 保持旧缺省快捷键语义，备份格式版本1及字段要求不变。`restoreDefault` 仅调用目标字段的 `setSetting`，复用普通保存队列、冲突及失败回滚，不调用批量导入或全表草稿提交。其他字段未完成输入继续保留；正常离开其他文本框的自动保存仍按原规则执行。当前隐藏的五个偏好不新增单项入口。

`MainWindow::restoreDefault` 只负责历史／剪贴板目录和降低配额的默认No确认，返回是否已发起恢复；普通保存目录只影响未来输出，不触发历史迁移。鼠标恢复按钮采用 `Qt.TabFocus`，不抢先触发编辑完成。本行 `restoringDefault` 期间抑制模态失焦提交，数字框先用 TextInput 的 clear／insert 暂存未提交文本，避免内部值在确认前变成较小配额；取消时还原文本，保留绑定和后续正常提交。不要改成直接赋值 `text` 或仅屏蔽一次 `valueModified`，前者破坏绑定，后者会让内部值先被修改。`SettingsDraft` 用单个字段来源标记清除同字段本地模板校验错误，避免已是默认值的无保存恢复仍残留错误；后端完成或新保存接管状态时清除来源，不能抹掉后端失败及其他字段错误。

ST-109 首增量使用独立的 `platform/startup.h` 与 `startup_mac.mm`，避免把本机系统登记加入普通偏好字段或备份版本1。`SMAppService.mainAppService` 仅在用户操作时 register／unregister，查询映射 Disabled／Enabled／RequiresApproval／NotFound；非APPL应用包或macOS13以前明确Unavailable，其他平台暂由stub禁用。待批准不通过反复注销／登记绕过用户决定，用户可显式打开系统登录项；NotFound仍允许尝试重新登记或取消。错误沿用系统反馈，不手写LaunchAgent或启动shell。

MainWindow只缓存当前系统状态及本行操作错误，`StartupRow.qml` 负责通用页顶部的共享控件与状态反馈；不占用Draft的保存状态，不提交其他草稿。显示／激活／进入General／语言变化时重读，不轮询。状态真正改变后清除旧错误；这是由外部启用回归验证的行为，不能只在重试成功时清除。真实原生API测试与UI fixture分开，fixture通过不代表系统实际登记成功。

Mac非Xcode开发构建在链接后对Still和独立原生测试包做完整ad-hoc签名；图标和生成Info.plist是链接依赖，避免资源单独变化使seal失效。Xcode保留其原生签名流程，部署／正式发布产物仍需独立验证。原生测试包使用专属 `local.still.startup-regression` 身份，执行登记前核对真实bundle ID；只有 `STILL_STARTUP_REGISTRATION_TEST=1` 才临时登记，并验证注销清理，普通CTest明确跳过这一步。不要用生产Still身份做自动启动验收。重启／重新登录、系统撤销批准及Finder／跨卷移动仍未实测。

ST-109补充 `startup_move_native_test.cpp`：`STILL_STARTUP_MOVE_TEST=1` 门控，独立UUID身份及临时签名包，后台验证同卷文件系统重命名后系统仍报告Enabled，在新路径可关闭、重新登记，并最终确认注销后清理目录。`startup-move-native-results.txt` 为4 passed／0 skipped、1009ms，根代理另行确认临时目录已移除。此结果不意味着移动必定产生NotFound，也不替代Finder／LaunchServices移动、跨卷移动或实际登录启动。

原生界面测试会影响当前桌面。优先完成离屏验证，再将必要原生场景合并为单个串行进程，不反复单项启动裸GUI可执行文件。结束后检查自己启动的精确进程；用户已反馈大量通用exec图标干扰Dock。此次检查确认测试进程已退出、Dock固定项没有新增测试项目，随后仅刷新当前用户Dock，未退出其他应用或修改固定项。后续不要为清理测试而批量终止用户应用或清空Dock偏好。

ST-109 Windows源码使用当前账户SID限定的Task Scheduler 2.0任务，`startup_win.*` 只拥有COM及系统调用，`startup_task.*` 单独拥有任务XML生成和检查；两者不重复登记规则。只使用InteractiveToken／LeastPrivilege、当前账户Logon触发及当前可执行文件，不经shell、不提权、不要求密码，不自动转换Run项或改StartupApproved。Source＋Documentation是本产品标识，异属任务拒绝改动；旧路径可显式重登记或删除，更新不先删除旧任务。任务和触发器Enabled、身份、动作及电池／运行时限条件都参与检查，不能仅凭登记存在判断开启。

Windows原生测试 `startup_windows_test.cpp` 为无界面、独立GUID＋SID＋UUID任务，需 `STILL_WINDOWS_STARTUP_TEST=1` 才修改自有测试登记；不打开任务计划程序。当前Mac缺少Windows SDK／MinGW／Qt Windows，尚无Windows编译、真实XML导出、系统登记或重新登录通过记录。可运行的公共XML测试只证明本机解析和文件路径检查；静态官方头文件核对不替代Windows验收。

共享界面通过StartupStatus.settingsLabel显示平台管理入口；Windows明确为任务计划程序，Mac仍沿用系统登录项。操作完成与被动刷新分开：被动外部状态改变会清旧错误，本次操作返回警告则保留警告再显示真实读回状态，不能因为状态变化就把本次失败清掉。系统API可能部分生效，不承诺任务服务异常时的事务性回滚；不先删除旧任务，不乐观改缓存，不在错误后强行覆盖用户系统状态。

ST-110 首个实际 API 功能由用户选择为 OCR 文字校对。`api_proofreader.*` 单独负责 Qt Network 请求与响应边界；`api_proofread_dialog.*` 拥有文字快照、显式发送、取消、结果预览／编辑与应用；`ocr_result_dialog.*` 负责选区 raw 范围映射和一次可撤销替换。通用偏好底部经 `MainWindow::openApiSettings` 打开 `ApiSettingsDialog`，OCR 预览也可打开同一设置；不增加右键或菜单栏菜单项。系统 OCR 和本机换行／空格整理继续无需密钥，打开窗口或保存设置不发送 OCR 内容。

选区使用当前校对稿的 UTF-16 文档范围，映射回保留 CRLF 的 raw 文本；无选区时取完整校对稿。窗口模态预览保护源范围，只在点击发送后提交固定校对指令与该快照，不附图片、历史或其他上下文。返回结果先显示，再明确应用；未编辑结果直接采用服务 raw 文本，手工编辑使用 Qt 文档 raw 字符，避免 `toPlainText()` 将 NBSP 等规范化。应用经原有 `replaceEditedText` 单次编辑块提交，保留选区外 raw 字符与独立原始结果，可撤销，不回写图片或贴图选字坐标。

`settings/api_profile.*` 只将完整 endpoint 和 model 原子保存为独立 `api-profile.json`；不向普通偏好或严格版本1备份添加字段。服务地址是完整 Chat Completions URL，不自动拼路径，不预设供应商或模型。请求采用单一 OpenAI 兼容非流式协议；远端只接受 HTTPS 且要求密钥，本机回环可使用无密钥 HTTP。拒绝地址内账号、查询参数和片段以及非法密钥字节；密钥上限2560字节。输入上限65536个UTF-16字符、响应上限1 MiB、时限60秒，手动处理重定向，不自动重试或切换服务。空白、拒绝、截断和无效响应不产生可应用结果；用量缺失显示未知，不估算费用。取消停止本机等待，不能承诺撤销服务处理或计费。

`platform/api_credentials.*` 以完整 endpoint 的 SHA-256 为账户隔离密钥，固定服务名 `local.still.api-key`。Mac 使用当前 ad-hoc 开发签名可用的用户默认 file-based 登录钥匙串，禁同步、不弹授权界面、保留系统 ACL；操作期间短暂禁用进程 Keychain 交互并恢复原值，不放宽 ACL 或退回文件。重新签名后旧项可能要求系统授权，此时明确报错；不宣称已使用 data-protection／ThisDeviceOnly。Windows 使用当前用户 Credential Manager、同机持久化，源码尚未编译或原生执行。界面不预填已有密钥，地址变化清除未保存密钥，不跨地址复用；保存先写密钥，再写 profile，后一步失败明确说明密钥已保存，不虚称跨系统事务回滚。删除只处理当前地址密钥，不提交其他输入。

连接测试必须显式点击，只发送 `Still API connection check.` 固定样例，支持取消，不保存未提交配置；请求文字、结果、用量和密钥不写普通配置或日志。回归采用 `api_test_server.h` 的回环 HTTP 模拟服务与 `api_credentials_fixture.*`，不调用真实远端、不访问用户密钥；工具沙箱不允许监听本地端口，测试需在允许回环监听的环境执行。原生凭据测试是 QCoreApplication，无 GUI，需 `STILL_API_CREDENTIAL_TEST=1` 才对独立 UUID 账户临时写入并精确清理。

首轮交付专项日志为 API 客户端57、profile14、API设置15、OCR结果35、原有UI126 passed，均无失败或跳过；Mac真实钥匙串后台测试4 passed、34ms，见 `api-credentials-native-unsandboxed-results.txt`。`api-delivery-ctest-results.txt` 为6/6通过、74.82秒，完整设置148 passed、17项既有环境／手工门控 skipped。首轮API入口用例遗漏恢复原页面，触发后续双Application组合崩溃；补齐清理后原组合8 passed，完整suite通过，所有产品布局诊断探针已撤回。未变化结果不新增撤销项，界面明确提示无需更改。翻译覆盖507 source／609 catalog、无缺失或占位符错误（`api-translation-results.txt`）；最后两条既有配置预览翻译补齐后仅重建Still，并通过bundle严格签名检查。首轮交付未启动原生GUI或操作Dock，后续超时和原生补验见下文；ST-110保持部分完成。

ST-110后续验收：`STILL_API_TIMEOUT_TEST=1` 是GUILESS真实60秒慢测门控，实测60.350秒、3 passed，验证单次超时、未知用量、无结果／自动重试以及显式重试；默认跳过慢测，客户端快测57 passed／1 skipped，见 `api-timeout-{native,default-gate,client}-results.txt`。测试容许默认CoarseTimer官方5%提前边界，未改生产时限。

API子窗完成后不能只对非活动编辑框setFocus。普通宿主回归实证原先activeWindow为null；现由拥有稿件的OcrResultDialog监听finished，singleShot(0,this,...)激活自身并聚焦edited，accepted仅提交数据，涵盖应用／取消／关闭，this销毁自动取消，隐藏窗口不会被重新show。修复前2pass1fail，修复后两数据行4pass；共享3组回归通过，OCR37／UI126／设置148pass+17skip（`api-focus-final-ctest-results.txt`）。当前源码已在原生隔离宿主复验：应用后不点宿主重新激活，立即Cmd+Z恢复原稿；取消及HTTP 401关闭子窗也直接返回校对编辑框。`api-focus-native-telemetry-results.txt` 的pid90982记录本进程实际activeWindow／focusWidget，三条路径均为OcrResultDialog／OcrEditedText，未以Cua窗口选择代替Qt焦点证据。

本轮Cua在独立隔离宿主实际验证原生API设置遮蔽字段、固定样例连接／用量、选区发送、手工编辑预览与应用、立即撤销及只读原始结果。持续等待的校对请求和设置连接测试均可取消并恢复控件；HTTP 401显示固定错误、不允许应用，原稿不变。宿主已构建运行当前产品源码，使用隔离profile、唯一回环服务和合成文字，未请求真实远端。QML入口接线已有integration回归；本轮原生补验未手工串联生产Still主窗口整流程，也不代表Windows通过。用户选择真实远端稍后配置，密钥不进聊天。

ST-107手工交接已结束：用户已将首行“Still OCR drag sample”拖入本机TextEdit文稿 `/private/tmp/still-p1-native-api-20261107/Still-P1-drag-target.txt`。Cua核对文字精确一致，接收端“撤销拖移”使文稿为空，重做恢复；保存后文件内容再次核对，Mac复制／文字拖出标准已验。OCR源三行均保留，本次未覆盖源端Move／删除撤销分支，该边界不追加为P1完成门槛。TextEdit和本轮宿主均已退出，隔离profile已清理，不再保留待拖动现场。P1其他剩余验收以迭代计划的核对表为准。


## 接手

1. 看根 `README.md` 的使用、构建和限制。
2. Windows 本机工具链位于 `.tools/`，运行 `scripts/build.ps1 -Test -Deploy`。Qt 套件需含 Widgets、QuickWidgets、QuickControls2；已有 qtbase 时用 aqt 补装 qtdeclarative，命令见 README。
3. 构建程序为 `build/Still.exe`，本轮实际交付 `dist/Still-refined/Still.exe`，退出旧托盘进程后运行。脚本默认目标仍是 `dist/Still/`；该处旧 exe 本轮无法覆盖，未强行终止用户进程，原目录保留旧包。Qt DLL、qml、plugins、native 与 exe 完整分发，不依赖 Node / Electron。应用 target `Still` 放在 `src/CMakeLists.txt` 避免扫描 `.tools/`，由 `qt_generate_deploy_qml_app_script` 部署 QML。macOS bundle为Still并携带ICNS，尚未Mac实测。
4. `legacy/electron/` 是原实现归档；旧交接文档见 `HANDOVER-ELECTRON.md`，其中“环境无法执行进程”的结论已经过期，不适用于当前会话。

## 用户已确认

- 支持截图、编辑、贴图、复制、保存，以及贴图上的文本选择。
- 同时考虑 Windows 与 macOS，因此从 Electron 改为 Qt。
- 设置与剪贴板：参考 Apple 的轻盈桌面界面，避免普通工程面板感。侧栏先剪贴板与截图历史，再四类偏好；设置只从侧栏进入，删除剪贴板页头的重复设置按钮。内联表单仅分节画线；剪贴板展示真实内容片段、按需阅读预览和单行常用操作。设置自动应用，无需保存按钮，输入完成后提交，失败恢复原值并保护新输入。截图和编辑工具栏保留原操作与快捷键。
- 品牌为拾影 / Still，英文不用拼音，自绘钴蓝取景框与一页Logo。CMake项目、全部构建目标、C++命名空间、QML URI、组织名和单实例标识统一使用Still；旧数据目录与归档标记兼容读取，保证历史与设置可继续使用。
- 通用偏好支持中文 / English与Light / Dark / System，默认zh_CN/system；保存成功后立即更新界面文案及调色板，Widgets标题菜单、画布和工具栏同步，截图图像与用户文本/文件名保持原样。
- 当前核心流程在本机处理；高级能力支持本机计算或用户自备 API Key，不引入会员、订阅或付费解锁。首个 API 功能已选择 OCR 文字校对，联网处理必须由用户明确选择；当前目标仅 P1，具体安排见 `ITERATION-PLAN.md`。
- 完整剪贴板管理方向：文字、图片、文件及更多格式；默认粘贴到原窗口，提供仅复制；历史本地持久化，可设数量、容量及独立目录。

## 代码职责

| 文件 | 职责 |
| --- | --- |
| `src/app/` | QApplication、单实例、托盘、主窗口入口、热键路由与截图动作交付 |
| `src/settings/` | 合并设置改动、协调热键预留与已提交状态；独立 API 地址／模型及系统凭据设置入口 |
| `src/capture/` | 冻结帧选区、放大镜、动作工具条、滚动截图会话与预览 |
| `src/clipboard/` | MIME 快照、归档、索引、配额、迁移、预览读取与原窗口粘贴 |
| `src/imaging/` | 图像计算、标注编辑器、贴图、OCR 任务与文字选择、显式 API 校对和预览 |
| `src/storage/` | 原子保存图片和 JSON、数据目录、独立贴图会话、截图历史查询、写入和迁移 |
| `src/platform/` | 跨平台热键预留、Win32／Carbon 热键、窗口捕获适配、系统 OCR、启动登记与密钥存储 |
| `src/ui/`、`assets/en.json` | QQuickWidget 桥接、设置草稿、界面本地化与图标 |
| `src/qml/` | 主窗口六页、侧栏、剪贴板、五类设置、共享控件和主题 |
| `assets/still.svg`、`scripts/generate-logo.ps1` | 项目自绘 Logo 及 PNG、ICO、ICNS 导出 |
| `native/Invoke-Ocr.ps1` | 返回 UTF-8 JSON 文本及词级坐标，路径规范化、释放位图和流 |
| `tests/` | 核心、界面、设置与真实系统集成测试 |

## 关键约定

- QScreen / QWidget 几何是逻辑坐标，QImage 与 OCR 是源图片像素；必须按实际 bitmap 尺寸映射，不假设与屏幕缩放系数总是相等。
- `OcrResult.text` 保存完整选择文本；`OcrWord` 只保存源图位置框和 `textStart/textLength`。范围使用 UTF-16 偏移，按文本顺序排列且不重叠；选择以引擎返回的框为单位，复制直接截取文本，不再补空格或换行。全选与复制全部文字保留首尾空白。按下只接受严格命中，拖选按到矩形的最短距离吸附。
- 共用设置的 OCR 语言带地区标识；macOS 按 Vision 支持的语言/文字前缀匹配，例如 `zh-Hans-CN` 对应 `zh-Hans`。Vision 每行原文直接写入选择文本，相同位置框合并其文本范围；Windows 脚本保留 `OcrLine.Text` 并按阅读顺序查找每个词的位置，两端均以 LF 分隔识别行。系统调用失败即使没有 NSError 也返回明确错误，真正的无文字图片仍返回空列表且无错误。
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
- Application 析构先提交设置草稿并销毁主窗口及QML内容，再销毁 SettingsApply、ClipboardController、ScrollSession、HistoryStore、ClipboardArchive，最后让 Hotkeys 成员结束生命周期。SettingsApply 析构时按顺序完成在途和排队的设置提交；HistoryStore 同步等待工作队列并交付配置结果，不运行界面事件循环。控制器以 QPointer 跟踪宿主，后台监听不依赖主窗口存活；MainWindow销毁时取消预览并解除宿主关联。
- 剪贴板归档在专用单线程队列读写。预览和显式操作独立取消，GUI 只保留元数据、小缩略图和当前预览。迁移先发布可读取的目标索引，再提交目录设置，成功后才清理旧副本。格式、文件引用和隐私约定见 `CLIPBOARD.md`。

## 验证与遗留

本轮已用 Qt 6.8.3 / MinGW 13.1 在 Windows 编译并通过八组非integration CTest；真实系统OCR/热键保留既有集成记录，本轮未重跑。最终测试报告在 `build/*-results.txt`，CTest 汇总在 `build/Testing/Temporary/LastTest.log`。

当前QML主窗口默认1000×700、最小800×620，200px分组侧栏、32px工作区左右内边距（窗口宽<900时24px）、24px/500标题。Windows使用Segoe UI并为Han指定Microsoft YaHei UI回退，Mac的Helvetica Neue未实测。导航可见顺序映射后台页号[0,3,1,2,4,5]，方向键同序。页头仅计数、暂停与预览；44px搜索、32px短筛选无贯通基线。内容标题15px/500、片段13px、类型日期11px，单行64px、图片76px、多行／文件80px，间距2px；真实多行文字首个非空行作标题，后续行作片段。阅读栏无灰色外卡与重复标题，仅左分隔与20px内边距，MIME仅日期tooltip。主动作深墨／夜间浅墨反色，底部只保留一次Enter提示。设置有说明行68px、无说明行48px，仅分节18+1+18，无逐行线；输入9px圆角，浅填色，焦点细边。历史列表仍随内容增高至260px，短历史首屏可见操作。

最新证据为 `qml-*.png`：23张Windows原生software渲染截图，覆盖中英文、深浅主题、预览、紧凑窗口、暗色按钮状态、独立迁移清理警告和真实临时文件URL的列表／预览。均使用隔离临时合成数据，无用户真实历史，不随产品发布。offscreen截图字体不可用，不作视觉证据；contact表仅作概览。设计规范见 `PRODUCT.md`、`DESIGN.md` 与 `qml-shell.md`；旧截图和验收记录只保留历史意义。

工具栏渲染证据为 `*toolbar-current.png`；局部设计约定见 `toolbars.md`。界面测试覆盖图标绘制、中文无障碍名称、快捷键、按钮动作和窄窗口的溢出入口。

OCR 队列图片滞留和长截图启动失败首帧残留已修复，含修复前失败、修复后通过的回归测试。说明与探针前后数据见 `MEMORY-FIXES.md`。历史存储优化及长截图分配预算见 `HISTORY-STORAGE.md`；历史架构修复记录见 `ARCHITECTURE-FIXES.md`。当前共九组CTest，本轮Release构建、八组非集成测试通过。QML回归 `build/settings-results.txt` 为21 passed、0 failed，Windows原生 `build/settings-premium-native.txt` 为18 passed、0 failed、3个环境相关skipped；`build/settings-premium-hidpi.txt` 为Qt1.5倍真实鼠标／键盘侧栏与偏好4 passed，303条当前源字符串与305条英文catalog覆盖及占位符检查通过。本轮断言覆盖页头设置按钮移除、侧栏可见顺序键盘导航、真实多行标题／片段、文件路径显示及中英字体一致性。`premium-finish-review.md` 的ship仅覆盖本轮已评分项、文件路径场景及直接可见回归，共23张最终截图；`premium-direction-review.md`为独立结构诊断。

历史真实Windows粘贴证据为 `build/qml-native-paste-results.txt`：3 passed、0 failed、0 skipped，保留一次 `OleSetClipboard 0x800401d0` 短时占用QWARN；测试恢复备份最多重试10次并检查恢复结果，持续失败会QFAIL。本轮界面改动后未重放真实系统粘贴。

`build/premium-smoke-results.json` 记录本轮部署版 `dist/Still-refined/Still.exe` 在清空SDK PATH/QML/plugin搜索环境、独立 `.tools/smoke-premium` 配置、不监听剪贴板、Windows默认渲染后端（未强制software）的四组合启动：zh_CN-light、zh_CN-dark、en-light、en-dark均QML Ready、退出0。`build/Still.exe`与当前部署exe的SHA256相同：`92DBB4E7A36630716611FCD84184E50858E3E93D5299B8C43E60A8FB7AC3166D`。原 `dist/Still/` 因旧exe无法覆盖保留旧包；不得用旧smoke或旧hash代表本轮交付。这些是启动检查，不等同于GPU性能或完整硬件图形验收。

尚需 Mac 编译/运行、屏幕阅读器、多屏混合 DPI 实机验收、HDR、完整滚动页面、长期内存/延迟与GPU性能测试；系统粘贴仍受权限和剪贴板占用环境影响。未迁移的非核心设置和功能列在 README 的当前限制中，不要把保留字段当作已实现。
