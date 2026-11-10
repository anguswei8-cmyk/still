# Windows 验证记录

日期：2026-11-10。源码基线：`37201af`，另含本记录列出的 Windows 修复。

本轮完成 Windows 构建、自动化回归、原生专项和部署启动检查，不代表全部 Windows 人工验收完成。

Ctrl+1 修复后的功能复审又发现并修复了贴图弹窗销毁闪退和普通设置路径校验遗漏；最新结果与新产物位置见 [Windows 功能复审](WINDOWS-FUNCTIONAL-REVIEW.md)。

## 环境与结果

- Windows 11 专业版 x64，系统版本 `10.0.26300`。
- Qt 6.8.3 / MinGW GCC 13.1.0，Release；CMake 4.4.4、Ninja 1.13.2、Python 3.13.13。
- 工具链位于忽略目录 `.tools/`，未改全局 PATH；测试使用临时目录或 `STILL_DATA_DIR` 隔离数据。
- 完整构建通过。首轮全量 CTest **26/26 组通过**，159.36 秒；QtTest 合计 **964 passed / 0 failed / 19 skipped**，计数包含各组初始化和清理。该首轮遗漏了真实全局快捷键消息分发，后续修复及追加验证见下文。
- 原生界面补验 **16 passed / 0 failed / 0 skipped**；最后的焦点等待调整另在 offscreen 复验 **3 passed / 0 skipped**。
- 翻译检查通过：**578 source strings / 609 catalog entries**，占位符保留。
- 部署产物为 `dist/Still/Still.exe`；移除 PATH 中的开发工具链后，使用 Windows 平台启动 `--smoke-test`，退出码 **0**。

## 已验证范围

| 范围 | 证据与边界 |
| --- | --- |
| 存储、图像处理、设置、贴图会话、剪贴板、API 客户端 | 全量 CTest 通过；大部分界面回归使用 offscreen，不代替原生操作 |
| Windows 凭据管理器 | 4 passed、无跳过；独立随机测试账户，包含 2560 字节读写、替换、超限拒绝和清理 |
| 任务计划程序 | 7 passed、无跳过；独立随机测试任务，覆盖查询不登记、启用／撤销、外部禁用、旧路径修复和外来任务保护；测试结束清理 |
| 系统 OCR、热键、长截图、跨窗口粘贴 | 原生测试 11 passed、无跳过；OCR 为合成英文图片及位置框；长截图覆盖中英深浅主题和捕获排除，粘贴目标为独立测试窗口 |
| 原生界面专项 | 窗口控制、微软雅黑字形、贴图关闭浮层层级、选区宽高及比例键盘操作、F2 改字、裁切／翻转、颜色菜单、亮度弹窗、会话恢复及真实退出子进程，共 16 passed、无跳过 |
| API 超时 | 显式启用真实 60 秒超时测试，验证只完成一次及后续显式重试；服务为本机模拟服务 |
| 部署 | Qt DLL、QML、Windows 平台／TLS 插件与 OCR 脚本已部署；冒烟检查实际加载主窗口 QML 后退出 |

19 个全量跳过项主要为 Unix 文件权限／符号链接、大小写敏感卷、macOS 专项，以及 offscreen 无法验证的原生场景。原生补验已覆盖其中的 Windows 窗口控制、字形及四种关闭浮层场景，其余跳过不计为通过。

## 本轮修复

1. `api_credentials_win.cpp`：MinGW 的 `CRED_MAX_CREDENTIAL_BLOB_SIZE` 仍为旧值 512，导致 2560 字节断言编译失败。按 [Windows 凭据文档](https://learn.microsoft.com/en-us/windows/win32/api/wincred/ns-wincred-credentialw) 的 `5 * 512` 上限断言，保留输入校验，并通过真实系统最大长度读写。
2. `startup_task.cpp`：系统回读 XML 会把触发器的 SID 写成 `DOMAIN\user`。现经 Windows 查询解析为 SID 再比对，避免成功登记后误报路径／条件失效；归属、权限和动作检查保留。
3. `settings_backup.cpp`：Qt 在 Windows 将 `C:relative` 判断为绝对路径。备份改为明确识别 Unix、带根目录盘符和 UNC 路径，拒绝盘符相对路径；现有失败用例复验通过。
4. 测试移植：HTTP 头名称按大小写不敏感匹配，凭据值仍精确比较；换行样例保证单词可容纳 offscreen 后备字体；设置测试在异步写入前检查原文件，避免自己的读取句柄阻止 Windows 原子替换；裁切原生测试等待实际活动窗口和画布焦点后发键。
5. 用户反馈 Ctrl+1 闪退后追加修复 `platform_win.cpp`：Qt 分发线程级 `WM_HOTKEY` 时传入空的 `result` 指针，原代码无条件写入，导致 `0xc0000005`。用户的原部署版崩溃偏移 `0x6ec91` 对应此写入指令。新增原生用例通过 `PostThreadMessageW` 走实际 Qt 消息循环，修复前 GDB 捕获同一回调中的 `SIGSEGV`，写入寄存器为 0；增加非空判断后用例 **3 passed**，完整原生组 **12 passed / 0 failed / 0 skipped**。应用与原生测试目标重编、重新部署，清洁 PATH 下运行部署版 `--smoke-test --capture` 退出码 **0**。本次只复跑受影响的原生组和截图启动检查，未重复无关的 26 组回归；未进行物理键盘输入验收。

## 复现与日志

工具链准备后，在项目根目录运行：

```powershell
$env:STILL_API_CREDENTIAL_TEST = '1'
$env:STILL_WINDOWS_STARTUP_TEST = '1'
$env:STILL_API_TIMEOUT_TEST = '1'
./scripts/build.ps1 -Test -Deploy
python scripts/check-translations.py
```

这三个门控会执行临时凭据写入、临时登录任务登记及真实超时等待，均使用测试身份。

日志保存在本机忽略目录 `build/`：

- `windows-build.log`、`windows-credentials-build-before.log`：首次构建失败。
- `windows-fixes-build.log`、`windows-final-ctest.log`：修复后构建及 26 组最终回归；`windows-test-summary.json` 为逐组计数。
- `api-credentials-results.txt`、`windows-startup-results.txt`、`native-results.txt`、`api-client-results.txt`：最终系统及 API 专项。
- `windows-native-ui-before.txt`、`windows-transform-focus-probe.txt`：原生焦点失败及诊断；`windows-native-ui-results.txt` 为修正后的 16 项通过，`windows-transform-offscreen-final-results.txt` 为相关离屏复验。
- `windows-deploy.log`、`windows-deploy-native-smoke-{stdout,stderr}.log`：部署与原生启动输出。原生冒烟进程退出码已核对为 0；Qt 日志可能为空。
- `windows-ctrl1-before-gdb.log`：追加用例修复前的真实线程消息崩溃栈；`windows-ctrl1-fixed-results.txt`、`windows-ctrl1-native-results.txt`：修复后的专项和原生全组通过。`windows-ctrl1-fixed-build.log`、`windows-ctrl1-deploy.log`、`windows-ctrl1-smoke-{stdout,stderr}.log` 为追加构建、部署及截图启动检查。

部署目录只包含 Windows 平台插件。早期将其强制设为 offscreen 的检查未完成、已终止，不计为通过；最终使用实际分发的 Windows 平台和独立数据目录验证。

## 剩余验收

- Computer Use 原生管道和备用界面通道均不可用；本轮没有人工点击或跨应用鼠标拖放验收。
- 实际注销／重新登录后的启动、安装包／正式分发、中文系统 OCR、Explorer 文件粘贴及生产者退出后再粘贴，仍待验收。
- 原生系统文件／确认面板、完整辅助功能、真实混合 DPI 桌面捕获／显示器热插拔、其他 Windows 版本、长期内存与性能仍待验收。
- 真实远端 API、真实密钥及计费未测试；本机模拟 API 不代表远端服务已验收。
- 部署工具提示 Qt 自带翻译及 `dxcompiler.dll` / `dxil.dll` 缺失。当前 Windows 原生启动通过，但不据此保证 Qt 内建面板本地化、D3D12 或其他设备的渲染路径；仍需正式分发专项。

保留首次失败和最终通过证据；验证过程未推送代码。
