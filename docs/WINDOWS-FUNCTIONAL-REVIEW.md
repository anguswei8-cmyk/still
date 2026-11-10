# Windows 功能复审

日期：2026-11-10。基线：`37201af` 加本轮 Windows 修复；Windows 11 x64、Qt 6.8.3、MinGW 13.1、Release。

本轮沿主要功能的入口、调用者、异步回调、存储提交及窗口销毁路径重新审查，并复跑全部测试。按单用户桌面使用评估；真实捕获验证环境为单屏。本轮又确认两类缺陷，均已修复；没有发现其他可确认的未解决阻断项。这个结论不代表所有设备、系统版本或人工操作均已验收。

## 发现及处理

### 1. P1：弹窗打开时关闭贴图会使进程崩溃（已修复）

- 功能：图片贴图保存、图片贴图右键菜单、文字／富文本贴图右键菜单。
- 问题：`QFileDialog`、`QMenu` 建在栈上，同时以贴图为 QObject 父对象。其 `exec()` 仍在运行时，全局关闭贴图或达到贴图数量上限可能销毁父窗口，Qt 随之提前删除这些栈对象，造成重复释放。三个独立进程复现均以 `0xc0000374`（堆损坏）退出。
- 修复：保存对话框及两个贴图菜单改为堆对象，用 `QPointer` 识别父窗口已销毁的情况，用作用域清理正常返回时的对象；保存对话框失效后直接取消，不继续读取图片或设置引用。保留现有同步交互。
- 后果：若保留原实现，使用“关闭全部贴图”时仍可能出现与 Ctrl+1 一样的整进程退出。
- 证据：新增 `UiTest::closingPinWithAnOpenPopupDoesNotCrash` 三个数据行，实际打开弹窗、关闭并销毁贴图、检查仅关闭一次且没有生成保存文件。离屏及 Windows 窗口平台均通过。独立复现程序换用修复后的真实对象文件，在 Windows 平台三项退出码均为 0，其中保存项使用系统原生文件对话框。

涉及 `src/imaging/editor.cpp`、`pin.cpp`、`text_pin.cpp`、`tests/ui_test.cpp`。

### 2. P2：普通设置和迁移入口漏放 Windows 相对目录（已修复）

- 功能：保存目录、截图历史目录、剪贴板目录及备份目录恢复。
- 问题：前轮只修正了备份文件解析。Qt 6.8.3 在 Windows 对 `C:relative`、`/rooted`、`\rooted` 都返回“绝对路径”，其他入口仍直接采用这个结果；保存页还接受普通相对目录。这些路径的真实目标依赖工作目录或当前盘符，换一种方式启动后，文件可能出现在其他位置，历史看起来消失。
- 修复：存储层提供统一的本机完整目录判断；Windows 要求带根目录的盘符路径或完整 UNC 共享路径。普通设置在提交前拒绝无效输入，历史与剪贴板工作线程也再次检查，备份恢复按本机规则检查。备份文件解析仍允许携带另一平台的路径用于预览。
- 后果：若只修改备份解析，用户在设置中直接输入相同路径仍会遇到不稳定的存储位置。
- 证据：设置页覆盖三个目录字段和五种无效路径，共 15 行；历史迁移追加盘符相对路径拒绝；剪贴板迁移检查失败时不提交且原历史仍能读取。原本在 Windows 被跳过的外平台备份目录测试已改为实际执行并通过。

涉及 `storage/store.*`、`history.cpp`、`clipboard_archive.cpp`、`settings_draft.cpp`、`settings_backup_dialog.cpp` 及对应测试。没有自动改写已有路径或迁移用户数据；遗留相对目录需要在设置中重新选择完整路径。

## 功能审查范围

| 功能 | 本轮核对内容 | 验证依据及边界 |
| --- | --- | --- |
| 启动、托盘、单实例 | 启动参数、实例请求、主窗口创建、退出顺序 | 实例请求测试、设置应用测试、部署原生启动；未做安装包验收 |
| 八个全局快捷键 | 注册／注销、冲突保留旧值、暂存提交、Windows 消息分发及应用路由 | `WM_HOTKEY` 真实线程消息回归及原生注册测试；未操作物理按键 |
| 区域／整屏／快速截图 | 隐藏与恢复窗口、选区交付、窗口吸附、尺寸、比例、坐标换算、取消 | core、ui、settings、native；混合 DPI 实际桌面与显示器热插拔未测 |
| 延时、重复、区域预设 | 倒计时取消、重复区域、屏幕匹配失败回退、坏索引保留 | delay、region_presets、capture_presets、settings |
| 标注和图像处理 | 11 种工具、选择／移动／改字、撤销重做、裁切、翻转、灰度／反色／亮度、OCR 失效 | core、ui、settings，原生键盘及弹窗补验 |
| 图片／文字／富文本贴图 | 拖动、选择复制、缩放、关闭提示、数量上限、菜单、关闭栈、会话恢复、正常退出保存 | pin_session、ui、settings；新增弹窗销毁回归；不保证崩溃／断电恢复 |
| 系统 OCR | 两任务队列、关闭／替换后的回调、文本偏移、词框、结果整理 | OCR 解析／结果测试及真实 Windows 英文识别；中文语言包未实测 |
| API 校对、密钥 | 显式发送、地址与密钥校验、手动重定向、取消／超时、响应上限、预览再应用、系统凭据 | 模拟 HTTP 服务、真实 60 秒超时、隔离 Windows 凭据测试；未调用真实收费服务 |
| 剪贴板 | 排除敏感标记、MIME 恢复、文件复制效果、富文本资源隔离、搜索／固定／预览取消、目标窗口粘贴保护 | clipboard、browse、clipboard_ui、settings、native；粘贴目标为测试窗口，Explorer 未验收 |
| 保存、历史及迁移 | 唯一文件名、PNG／JPEG、原子写入、复制为文件、历史配额、队列上限、迁移校验／回滚与删除顺序 | export、history、clipboard、settings_apply；磁盘满／权限拒绝主要由故障测试模拟 |
| 设置、备份和恢复 | 草稿校验、排队保存、失败回滚、快捷键冲突、默认值、跨平台路径、备份不含密钥 | settings_apply、settings_draft、backup、backup_dialog、settings |
| 登录启动 | 用户身份、任务归属、系统回读 XML、启用／禁用、路径修复、外来任务保护 | startup_task 及隔离 Windows 任务测试；未实际注销登录 |
| 主界面 | QML 入口、页面／筛选／快捷操作、键盘导航、窗口控制、翻译 | settings、原生专项、翻译检查；未做屏幕阅读器和全界面人工逐项点击 |

## 最终验证

- 完整 Release 构建成功。
- 修复后 CTest **26/26 组通过，163.23 秒**，显式启用凭据、登录任务及真实 API 超时测试。该次 QtTest 汇总 **984 passed / 0 failed / 19 skipped**，包含初始化和清理。
- 随后移除备份目录测试中的 Windows 跳过条件，只重编并复跑该组：**14 passed / 0 failed / 1 skipped**，比上述全量记录多执行了一个通过项。
- 新弹窗销毁测试：离屏 **5 passed / 0 failed / 0 skipped**，Windows 平台 **5 passed / 0 failed / 0 skipped**，其中三项为数据行、两项为初始化／清理。
- Windows 原生界面补验：**16 passed / 0 failed / 0 skipped**，覆盖窗口控制、字形、关闭浮层、选区宽高／比例、标注改字、图像处理菜单、亮度弹窗、会话恢复和真实退出子进程。
- 翻译检查：**579 source strings / 610 catalog entries**，占位符保留；`git diff --check` 通过。
- 新产物：`dist/Still-reviewed/Still.exe`。清除 PATH 中的开发工具链后，以隔离数据目录运行 Windows 平台 `--smoke-test --capture`，退出码 **0**；部署文件与 `build/Still.exe` 的 SHA-256 一致：`957DFF5804D92FF95B3D59320151085BB2961C15414DC20BA665E7EC328302D8`。
- 打包时原 `dist/Still/Still.exe` 仍在运行，没有覆盖或重启该进程。使用新产物前应从托盘退出旧版，避免单实例机制将启动请求转发给旧进程。

## 复现证据

本地忽略目录 `build/` 保留：

- `functional-review-probe.cpp`、`functional-review-{save,pin-menu,text-menu}-before.log`：三个旧实现的独立崩溃复现。退出码在执行记录中均为 `-1073740940`。
- `functional-review-{save,pin-menu,text-menu}-native-after.log`：修复后的 Windows 平台复验，三项退出码均为 0。
- `functional-review-popup-tests.txt`、`functional-review-popup-native.txt`：永久回归用例的离屏／Windows 结果。
- `review-path-probe.cpp`、`review-path-probe.log`：Qt 对 Windows 路径的实际判断。
- `functional-review-build-all.log`、`windows-reviewed-ctest.log`：最后的完整构建及全量测试。
- `review-backup-dialog-results.txt`、`review-native-settings-results.txt`：追加专项结果。
- `windows-review-deploy.log`、`windows-review-smoke-{stdout,stderr}.log`：新部署及清洁 PATH 检查。

## 尚未完成的验收

Computer Use 原生管道仍不可用，故没有手工串联所有入口。物理快捷键、混合 DPI／热插拔、中文系统 OCR、Explorer 粘贴、实际重新登录、其他 Windows 版本、长期运行、屏幕阅读器和正式安装包仍待验收；macOS 本轮未运行。图标绘制细节与其他平台实现没有重新逐行审查。

不能承诺“以后不再有 Bug”。本轮已把确认的崩溃和路径问题变成会实际失败的回归，并将离屏测试与系统级验证的边界明确记录。验证过程未推送代码。
