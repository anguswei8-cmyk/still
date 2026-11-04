# PixClone 交接文档（HANDOVER）

- 交接日期：2026-10-29
- 代码版本：`pixclone@0.1.0`，工作目录 `C:\Codes\PixClone`（非 Git 仓库）
- 运行环境：Windows 11 (10.0.26200/26300) x64，Electron ^33.2.0，Node >= 18
- 阅读顺序建议：本文件 → `README.md`（功能/交互/架构对照） → `docs/REA-FINDINGS.md`（PixPin 逆向证据）

---

## 0. 一分钟接手

```bash
cd C:\Codes\PixClone
npm install        # 只装 electron
npm test           # 18 个纯逻辑单元测试，不需要 Electron
npm start          # 启动：托盘常驻 + 全局热键 + 单实例
# 打包（需先手动装 electron-builder，见 §9 已知问题）
npm i -D electron-builder && npm run dist
```

接手第一步建议先跑 `npm test`，再跑 `npm start` 做一轮 §7 的手动验收清单。
上一轮交接者**无法在本机之外执行任何进程**（见 §6），所以 Electron 相关路径全部只做了静态审查 + 纯逻辑沙箱验证，**运行时验收尚未完成**。

---

## 1. 项目边界（不可违背的前提）

1. **不复制 PixPin 任何代码/资源**：不复用其二进制、图标、字体、QSS、文案、模型权重、素材、商标。图标由 `src/main/png.js` 运行时生成，快门声由 `clipboard.js` 运行时合成 WAV，OCR 用系统 `Windows.Media.Ocr`。
2. **完全离线**：无自动升级、无统计上报、无崩溃上报、无账号/云同步（对应 PixPin 的 `PixPinAuxiliary.exe` / `upgrade.pixpin.cn` / `stat2.pixpin.cn`，刻意不复刻）。
3. **不上传截图**：OCR 全程本地（临时 PNG 写本地 temp，PowerShell WinRT 处理后删除）。
4. **依赖最小化**：`devDependencies` 只有 `electron`；`tesseract.js` 为可选备用 OCR。
5. 只参考对 PixPin **行为的观察结论**（`docs/REA-FINDINGS.md`），证据分级 observed / derived / inferred。

---

## 2. 代码地图与职责

| 文件 | 职责 | 关键点 |
| --- | --- | --- |
| `src/main/main.js` | 装配入口：单实例锁 → 特权协议 → whenReady 内按序创建 settings/history/pins/overlay/scroller/settingsWindow/tray/hotkeys → `registerIpc()` | 启动顺序不能乱：`registerFrameProtocol()` 与 `registerPrivileged()` 在 `whenReady` **之前**；`frameProtocol.handle(temp, {history})` 在 `whenReady` **之内**（需要 dirs）；`hotkeys.registerAll()` 之后才 `settingsWindow.hotkeys = hotkeys` |
| `src/common/defaults.js` | `DEFAULT_SETTINGS`、`IPC`（27 个通道）、`ACTIONS`、`parseHotkey`/`toAccelerator`/`deepMerge` | 通道名唯一性有测试覆盖 |
| `src/main/capture.js` | 每显示器冻结一帧 → Temp PNG | `thumbnailSize` 按设备像素请求；**只向下**修正到该屏自身尺寸（绝不放大） |
| `src/main/overlay.js` | 每屏一个无边框全屏窗口（冻结画面 + 选区 + 标注）；`pixclone://` 特权协议；`sendInit()` 把 PNG 字节推给渲染进程 | `committed` 在 `start()` 复位；history 名称只禁路径分隔符与 `..` |
| `src/renderer/overlay/overlay.js` | 选区/吸附/8 手柄/放大镜/标注画布/工具条 | 帧用 IPC 字节 → `Blob` → `createObjectURL`；`pixclone://frame/...` 仅兜底；8 秒超时 |
| `src/main/pins.js` + `src/renderer/pin/*` | 贴图窗口（置顶、缩放、透明度、旋转、拖拽、右键菜单、blur 隐藏） | `pngOf()` 失败返回 `null`，上层必须判空 |
| `src/common/stitch.js` + `src/main/scroll.js` + `src/renderer/scroll/*` | 滚动长截图：定时抓帧 → 重叠检测 → 拼接 → HUD | 裁剪映射来自**解码后 bitmap 尺寸**，不是 `display.scaleFactor`；默认 420ms / 80 帧 / 24000px |
| `src/main/hotkeys.js` | `globalShortcut` 注册/注销/改绑/失败回报 | 单一注册来源；清空（`''`）是合法动作不是失败 |
| `src/main/settings.js` / `history.js` / `io.js` / `clipboard.js` / `ocr.js` / `png.js` / `toast.js` / `tray.js` / `settingsWindow.js` / `winspy.js` | 设置存储（原子写 + 点号路径 + `change` 事件）、历史索引、保存命名、剪贴板+WAV、OCR 调度、PNG 编码+图标、Toast、托盘、设置窗口、窗口枚举缓存 | 见 §5 决策 |
| `src/preload.js` | 唯一渲染桥（`contextBridge`，sandbox 开） | preload 的 19 处 `invoke/send/on` 全部有主进程对应（已核对）；另有几个已声明但未接通的常量/处理器，见 §11.1 |
| `native/Enum-Windows.ps1` / `native/Invoke-Ocr.ps1` | `EnumWindows`+DWM 扩展帧边界+cloak 过滤；`Windows.Media.Ocr` | 句柄转换必须 `(ulong)(long)h`；打包时靠 `asarUnpack` 落盘 |
| `test/run-tests.js` | 18 个纯逻辑测试（无 Electron） | 见 §6 |

数据目录（`app.getPath('userData')` = `%LOCALAPPDATA%\PixClone`）：

```
settings.json          原子写（settings.json.tmp -> rename）
History\               history.json + <id>.png（id = `<Date.now()>-<base36 6 位>`，见 `history.js add()`）
Temp\                  shot-<stamp>-<index>.png（冻结帧）、ocr-<ts>.png（OCR 临时）、滚动帧（解码后即删）
Data\                  预留
Screenshots\           未配置 save.dir 时的默认保存位置
```

---

## 3. 三条主链路（改代码前先看这个）

**A. 区域截图 → 动作**
```
热键/托盘 → actions.capture → startOverlay({action: capture.afterSelectAction})
  → overlay.start(): cleanTemp → captureDisplays(每屏 PNG) → 每屏建全屏窗口
  → 渲染进程 overlay.ready() → 主进程 sendInit(webContentsId) 推 PNG 字节 + bounds + workArea + scaleFactor + imageWidth/Height
  → 渲染进程 Blob URL 显示冻结帧 → 框选/吸附(listWindows) → 标注 → Enter/Ctrl+S/Ctrl+P/Ctrl+O
  → overlay:commit(payload{action, png bytes, region(DIP), device}) → main.handleShotResult
      · region 是 DIP；device 是设备像素（尺寸展示/历史/Toast 用 device）
      · copy → clipboard.writePngToClipboard + history.add(label='copy') + shutter
      · save → io.savePng（碰撞加 -2/-3…）+ history.add
      · pin  → pins.create
      · ocr  → clipboard.writeText + history.add(label='ocr')
      · draft→ history.add(label='draft')
  → 若 pins 存在则 setHidden(false) + syncTray()
```

**B. 滚动长截图**
```
Ctrl+5 → overlay 选区 → action 'scroll' → scroller.run(region)
  → 每 scroll.intervalMs 抓帧（captureDisplays 单屏）→ nativeImage 解码 → frameFromImage（BGRA→RGBA 自动判定）
  → 用 bitmap.width/bounds.width 反推 sx,sy → cropRaw(DIP→bitmap 坐标) → appendFrame 重叠拼接
  → 达到 scroll.maxFrames / maxHeightPx 或用户停止 → encodePng → onFinish
  → 写剪贴板 + history.add + pins.create（region 为 DIP，尺寸提示为设备像素）
```

**C. 贴图（pin）**
```
pins.create({pngBuffer, region(DIP)}) → BrowserWindow(透明/置顶/frame:false) + dataURL 传图
  → 渲染进程 pin:ready('hello') → 主进程 sendInit(settings 快照)
  → 命令走 pin:command {id, cmd, arg}（move/zoom/rotate/opacity/close/top/copy/save/ocr）
  → 图像回传：executeJavaScript(canvas.toDataURL) → pngOf() → clipboard/io/ocr
  → 截图选区期间 pins.setHidden(true)，结束/取消后恢复；pin.hideOnClickOutside 在 blur 时读设置
```

---

## 4. 关键设计决策（含理由，改动前请确认理解）

1. **先冻结再选区**（对应 PixPin `ScreenShotView::prepareScreenShotBgLayer`）：截图时画面里不会出现自己的工具条/光标。
2. **DIP 与设备像素分层约定**：窗口几何、选区、贴图 bounds 一律 DIP；图像字节、`capture.maxEdge`、历史里记录的宽高、Toast 文案一律设备像素。跨层必须显式换算（`pasteAsPin` 用 `scaleFactor` 把剪贴板图换算成 DIP 再定窗口）。
3. **滚动裁剪映射用"解码后 bitmap 尺寸"**：`desktopCapturer.getSources` 只能给所有 source 传**同一个** `thumbnailSize`，小屏帧可能被放大/缩小；用 `sx = bitmap.width / bounds.width` 反推才与真实字节一致。
4. **冻结帧优先走 IPC 字节 + Blob URL**，`pixclone://frame/…` 只兜底：自定义协议加载的图会**污染 canvas**，一旦污染 `toBlob()/getImageData()` 全废（导出、OCR、取色都失效）。
5. **整屏截图按显示器分别产出一张结果**（不是拼成虚拟桌面一张）：保留每屏真实尺寸，避免整桌面重编码。
6. **热键只有一个注册来源**（`HotkeyManager` + `globalShortcut`）：托盘菜单只把组合键当**文本**展示，绝不加 `accelerator`（否则重复注册）。
7. **改绑失败必须保留旧热键**：`setHotkey` 失败时回滚旧 accelerator 并把失败项排在最前，渲染进程只提示"已保留原热键"，不再自行改 UI 状态。
8. **native PowerShell 靠 `asarUnpack: ["native/**"]` + 路径把 `app.asar` 替换为 `app.asar.unpacked`** 才能在打包后可执行。
9. **保存文件名碰撞自动加序号**：`{index}` 不跨会话维护，同名同秒必须消歧，不能静默覆盖。
10. **设置变更镜像集中处理**：`main.js` 的 `applySetting` 处理 `history.maxItems`（trim）与 `ui.tray`（建/销托盘），批量变更（key 为空）时全量重套。
11. **OCR 优先系统 WinRT**（`Windows.Media.Ocr`），`tesseract.js` 仅在 `ocr.provider=tesseract` 时动态 `require`，失败回退并给可操作提示。

---

## 5. 已修复问题清单（按轮次，便于回溯"为什么现在长这样"）

### 5.1 早期轮次（结构与首版问题）
- 主进程/渲染进程拆分与 preload 通道契约建立；`contextIsolation` + `sandbox: true`。
- 贴图窗口透明/无边框/置顶、`skipTaskbar`、`resizable:false` 组合修正。
- 标注工具条与快捷键（R G O L A P H M B T D、撤销重做、序号工具）实现。
- 窗口吸附数据源从"猜测"改为 `Enum-Windows.ps1` 真实 `EnumWindows` + `DWMWA_EXTENDED_FRAME_BOUNDS` + `DWMWA_CLOAKED` + `SetProcessDPIAware`。
- 热键占用检测与失败提示（对应 PixPin `Setup failed, the hotkey may be occupied.`）。
- 历史存储重启后索引重载 + 失效文件清理。
- 打包 `files`/`asarUnpack`/`portable` 配置，`private: true`。
- PowerShell 句柄 64 位安全转换：`(ulong)h` → `(ulong)(long)h`。

### 5.2 上一轮（运行时正确性）
- **热键清空不再被当作失败**：`registerOne(name, '')` 走注销 + 清理失败项。
- **改绑失败回滚旧热键**（`setHotkey` 记录 previous 并在失败时重新注册）。
- 渲染端失败提示改为"设置失败：…，已保留原热键"，不再本地乱改状态。
- `pin.hideOnClickOutside` 设置项 + blur 时读设置（改了不用重启即生效）+ 一次性提示 Toast；`setHidden(false)` 时 `show()+focus()`。
- `pin.wheelZoom=false` 在渲染端直接不再发 zoom 命令。
- `capture.afterSelectAction` 真正决定 Enter 的默认动作。
- 新增 `settings.on('change')` 动态处理 `history.maxItems`、`ui.tray`。
- 整屏截图改为每屏一条结果；历史/Toast 尺寸改用 `device.w/h`。
- `pasteAsPin` 在缩放屏上按 `scaleFactor` 换算 DIP，贴图尺寸不再错。
- 托盘菜单去掉 `accelerator`，组合键改为文本展示。
- `ocr.js` / `winspy.js` 的脚本路径做 `app.asar → app.asar.unpacked` 替换。
- `scroll.js` 增加 `detectChannelOrder()`：实测 `nativeImage.getBitmap()` 通道序（Windows 常为 BGRA），必要时换通道，避免拼接结果红蓝互换。
- overlay 初始化握手：`overlay.ready` → `sendInit()` → 渲染端 Blob URL（含 8 秒超时与协议兜底）。

### 5.3 最近一轮（本轮，13 项）
| # | 文件 | 症状 → 修复 |
| --- | --- | --- |
| 1 | `src/common/defaults.js` | `IPC` 缺 `sound` 键 → `ipcMain.handle(IPC.sound)` 注册在 `undefined` 通道，快门声**一直静默失效**；补 `sound: 'ui:sound'` |
| 2 | `src/main/scroll.js` | 裁剪用 `display.scaleFactor` 而帧实际被共享 `thumbnailSize` 缩放 → 多屏/混合 DPI 裁剪错位；改用 bitmap 自身尺寸反推 map，最终 DIP 尺寸同 map；每帧 PNG 解码后即删（原来 80 帧可堆几百 MB） |
| 3 | `src/main/capture.js` | 小屏帧被共享 thumbnailSize 放大 → 模糊且尺寸虚高；增加**只向下**修正到该屏设备像素 |
| 4 | `src/main/clipboard.js` | WAV 头自相矛盾（8-bit 声明 + blockAlign 2 + 隔字节稀疏样本）→ 改规范 16-bit 单声道 PCM |
| 5 | `src/main/io.js` | `{index}` 从不维护 → 同秒两次保存静默覆盖同一文件；碰撞自动加 `-2/-3…`（上限 500） |
| 6 | `src/main/main.js` | 保存失败无反馈且在 commit 路径抛出 → try/catch + `保存失败：…` |
| 7 | `src/main/main.js` | 托盘双击/连按热键把正在框选的选区重置 → `startOverlay` 加 `if (overlay.active) return` |
| 8 | `src/main/main.js` | 设置批量变更（key 为空）被忽略 → `applySetting` 全量重套 `history.maxItems`/`ui.tray` |
| 9 | `src/main/overlay.js` | `cancel()` 后 `committed` 残留 `true` → 下一次 overlay 非提交关闭时不触发 `onCancel`，**贴图可能一直隐藏**；`start()` 复位 |
| 10 | `src/main/overlay.js` | history 名称正则 `[A-Za-z0-9._-]` 拒绝中文/本地化文件名 → 设置面板缩略图 403；放宽为"禁路径分隔符与 `..`" |
| 11 | `src/main/pins.js` | `pngOf/copy/save/ocr` 无错误处理（窗口中途销毁、目录不可写 → 未处理 Promise 拒绝）；空 dataURL 未判失败 → 全 try/catch + Toast |
| 12 | `src/main/hotkeys.js` | `registerAll()` 未清 `failed` → 陈旧失败项残留 |
| 13 | `toast.js` / `settingsWindow.js` / `renderer/pin/pin.js` | `showInactive` 前查 `isDestroyed`；`hotkeys` 未就绪时 `state()` 不再崩；`img.onload` 提前于 `img.src`（避免漏 load 事件导致导出空白） |

---

## 6. 验证现状

### 6.1 自动化测试（`npm test`，18 项，纯逻辑，无需 Electron）
```
parseHotkey parses modifiers + key
toAccelerator produces Electron accelerators
default hotkeys are valid accelerators
deepMerge keeps defaults and applies overrides
IPC channel names are unique
findOverlap detects a 60px overlap
appendFrame appends only the new rows
identical frames are skipped (no duplicate rows)
no overlap (jumped content) falls back to appending
width mismatch is normalized left-aligned and still detected
uniform page margins do not create a false overlap
a three frame chain stitches to the expected height
encodePng writes a decodable PNG
iconBuffer produces a valid runtime icon
settings merge defaults, persist and reload
settings emits change events
history add / list / remove / clear
history survives a restart (index reload + missing-file pruning)
```
> **交接者未能亲自运行 `npm test`**（环境限制见 §8），测试文件本身语法已核对，被测模块另用沙箱 shim 独立验证过（见 6.2）。

### 6.2 已在沙箱中独立验证（从真实源码抽取 + shim 执行）
| 项目 | 结果 |
| --- | --- |
| 25 个 JS 文件语法解析 | 全部通过 |
| `pixclone://` 协议处理器（真实 handler 体 + `path/net/URL/Response` shim） | **16/16**：中文名 history 文件允许；`..%2F`、`%2E%2E`、子路径、`%5C`、`%00`、错扩展名、未知 host 全部拒绝 |
| 滚动裁剪映射（真实 `cropRaw` + `Buffer.alloc` shim） | **6/6 逐像素正确**：2× 放大帧、0.5× 缩小帧、副屏 bitmap==DIP、副屏设备像素帧、越界钳制、600×4500 设备像素 → 400×3000 DIP |
| 默认热键 → Electron accelerator | **7/7**（`Ctrl+Shift+V → CommandOrControl+Shift+V` 等） |
| `io.buildName` + 碰撞后缀 | pattern 展开、非法字符清洗、空 pattern 回退、`a/a-2/a-3 → a-4` 均正确 |
| 快门 WAV 合成 | 头字段自洽、峰值 0.70、0 削波、50 ms 后衰减到 ~0 |
| PNG 编码器（`Uint8Array` Buffer shim + 存储式 zlib shim） | CRC32 KAT `0xCBF43926`；`IHDR/IDAT/IEND` CRC 正确；`7x5`、bitDepth 8、colorType 6；扫描线往返一致；`iconBuffer(32)` 合法 |
| 拼接算法（`Uint8Array` shim） | 常见用例全通过：60px 重叠、只追加新行、重复帧跳过、跳变退化、变宽归一化、均匀白边不误判、三帧链高度 |
| preload ↔ 主进程 IPC 通道 | 19 处 `invoke/send/on` 全部有对应 `handle/on/send`（含修复后的 `ui:sound`） |
| preload API 覆盖渲染端调用 | 缺失项：无 |

### 6.3 **尚未验证**（只能靠本机运行）
所有依赖 Electron 运行时/真实屏幕/真实剪贴板/真实 WinRT 的路径：overlay 握手与 blob 不污染 canvas、贴图 blur 隐藏、托盘标签与点击、粘贴为贴图在缩放屏的尺寸、打包后 `app.asar.unpacked/native/*.ps1` 可执行、OCR 真实识别、窗口吸附精度、16-bit 快门声可听。→ 请用 §7 清单验收。

---

## 7. 手动验收清单（接手后请逐条打勾）

启动与热键
- [ ] `npm start` 后托盘出现图标（运行时生成的 16px 图标），提示"PixClone 已启动…"
- [ ] `Ctrl+1/2/3/4/5`、`Ctrl+Shift+V`、`Ctrl+Shift+Q` 全部生效
- [ ] 把某热键改成被占用的组合（如 `Ctrl+Shift+F12` 若被占）→ 提示"设置失败：…，已保留原热键"，**旧热键仍可用**
- [ ] 清空某热键 → 不出现失败标记，且该功能不再响应；重启后仍为空

截图主流程
- [ ] `Ctrl+1` 框选 → Enter 复制 → 到画图/微信验证内容完整、**颜色正常**（无红蓝互换）
- [ ] 缩放 150% 屏上框选 → 保存后文件像素尺寸 = 选区 DIP × 1.5
- [ ] 同秒连续保存两次 → 得到两个不同文件（`xxx.png` 与 `xxx-2.png`），无覆盖
- [ ] 把 `save.dir` 指向不存在/不可写目录 → 出现 `保存失败：…`，程序不崩
- [ ] 标注（矩形/箭头/文字/序号/马赛克/荧光笔）+ `Ctrl+Z/Ctrl+Y` 后导出，结果与画面一致
- [ ] 放大镜取色 `C` 复制颜色；OCR `Ctrl+O` 能复制文字（本机 WinRT 可用时）

多屏 / DPI
- [ ] 混合 DPI 多屏：`Ctrl+3` 每屏各出一张贴图，尺寸与各屏一致
- [ ] 在副屏（不同缩放）上做滚动长截图 → 拼接不错位、不重影、不偏色
- [ ] 主屏上跨屏选区（若允许）不出现裁歪/缩放错误

贴图
- [ ] `Ctrl+Shift+V` 在 150% 屏上贴图尺寸正确（不是一半也不是两倍）
- [ ] 滚轮缩放以光标为锚点、`Ctrl+滚轮` 调透明度、双击恢复原尺寸、右键菜单全可用
- [ ] 设置里关闭"滚轮缩放"后贴图不再响应滚轮
- [ ] 开启"点击贴图外时隐藏"→ 点别处即隐藏，`Ctrl+4` 或托盘"显示全部贴图"能恢复
- [ ] 截图选区期间已有贴图自动隐藏，取消/完成后恢复（**不会卡在隐藏状态**，本轮修过 `committed`）
- [ ] 贴图窗口销毁/关闭过程中点复制/保存/OCR → 出现失败 Toast，不产生未处理拒绝

其它
- [ ] 快门"咔嚓"声能听到（本轮改为 16-bit PCM）
- [ ] 托盘双击图标不会把正在框选的选区重置
- [ ] 设置 → 历史：缩略图能显示（含中文文件名）；删除/清空/打开目录可用
- [ ] `history.maxItems` 改小后立即裁剪，无需重启
- [ ] 关闭"显示托盘图标"后托盘消失、重新开启后恢复，无需重启
- [ ] 设置 → OCR → "检测 OCR 可用性" 通过（本机 WinRT）
- [ ] `npm i -D electron-builder && npm run dist` 成功；解压/运行 portable 包后 OCR 与窗口吸附仍可用，并确认包内存在 `resources/app.asar.unpacked/native/Invoke-Ocr.ps1`、`Enum-Windows.ps1`

---

## 8. 环境限制（上一任交接者卡住的地方，别再浪费时间）

1. **直接 shell 不可用**：任何 `bash` 命令（含 `echo`、`dir`）返回乱码 + `E_UNEXPECTED`、exit 1。
2. **REA 进程捕获不可用（Windows）**：`mcp__rea__capture_process_scenario` 返回 `capability_unavailable / unsupported_provider`（"Windows PTY process capture is unavailable…"）。文档说明只支持 Linux/macOS（含 WSL 里的 Linux 命令），重装 PTY 后端无效。
3. **codemode 沙箱能力有限**：无 `require/module/process/Buffer/zlib/fs/crypto/TextEncoder/Intl/URL/TextDecoder`；有 `Uint8Array/BigInt/Map/Set/Promise/new Function`。
   - 变通办法（本轮就是这么验证的）：`tools.read` 取源码 → 截取目标函数片段 → `new Function('Buffer','path','net', …)` 注入 shim 执行；`Buffer` 用 `Uint8Array` + 自实现 `copy/alloc`；zlib 用"存储块（stored block）"自实现 inflate/deflate；`URL/pathToFileURL/net.fetch/Response` 手写 shim。
   - 结论：纯逻辑可验证，**任何需要 Electron/真实屏幕/进程的东西不可验证**。
4. 因此：**`npm test` 与 `npm start` 必须由人类在目标机上跑**，交接者只能做静态审查 + 纯逻辑沙箱验证。

---

## 9. 待办（按优先级）

### P0（影响可用性，先做）
1. 完成 §7 手动验收（尤其多屏/缩放屏、滚动截图颜色、贴图隐藏不卡死）。
2. `npm test` 跑通并把结果记录到本文件 6.1 节。
3. 打包验证：`electron-builder` 不在 `devDependencies`（刻意保持最小依赖），`npm run dist` 前必须 `npm i -D electron-builder`；确认 `asarUnpack` 生效。

### P1（已知缺陷，有明确线索）
4. **拼接两个边界用例未解决**（沙箱合成暴露）：
   - 窄累加图 + 宽帧：内容失配 600（`appendFrame` 左对齐归一化 + `concat(padX=0)` 后仍有内容错位）。
   - 静态带场景：overlap 报 90，期望约 40。原因在 `findOverlap` **从最大重叠向下搜索、第一个 ≤ threshold 就接受**，静态行在任何偏移都算"匹配"，于是吃进了静态带。
   - 可调参数（都在 `findOverlap(a, b, opts)`）：`minOverlap` 默认 12（下限 4）、`threshold` 默认 6.5、`cols` 默认 48；退化接受阈值是 `threshold * 3`（标记 `weak`）；`overlap >= b.height` 时整帧跳过（`skipped`）。
   - 建议方向：给候选 overlap 增加"必须包含至少 N 行**非静态**内容"的约束（即检测候选重叠区间内的行是否与其上下行完全相同），或让 `appendFrame` 传入更严格的 `threshold/minOverlap`；先加断言型测试再调参。
   - 复现方式：仿照 `test/run-tests.js` 的合成页面生成器构造这两类页面，断言拼接结果与"真值页面"逐像素一致（现有测试只断言高度/重叠，没断言内容）。
5. `src/main/winspy.js` 的 `BLACKLIST` 写的是进程名（`explorer.exe` 等），但 `Enum-Windows.ps1` 只输出 `hwnd/pid/title/cls/rect` → 这些项**永不命中**（死代码）。若发现桌面窗口（`Progman`/`WorkerW`）干扰吸附，改为按 `cls` 过滤。
6. 渲染进程 HTML 目前**无 CSP**（`src/renderer/*/index.html`）。内容全本地、风险低，但接手后建议加最小 CSP（`default-src 'self'; img-src 'self' blob: pixclone:; script-src 'self'`）。

### P2（增强，非阻塞）
7. 截图内合成鼠标光标（PixPin 有 `PixCursorPixmap`，本项目未复刻）。
8. 剪贴板写文件组（`CF_HDROP`）以支持"粘贴到资源管理器"。
9. 滚动截图：固定表头/虚拟滚动列表的识别（PixPin 的滚动截屏在这类页面也会退化）。
10. 历史项增加标签/收藏/搜索（PixPin 有"收藏/稍后阅读"语义）。
11. 给设置面板加"恢复默认设置"按钮：`Settings.reset()` 已写好，只需加 `IPC.settingsReset` + `settingsWindow.js` handler + `preload` 方法 + 渲染按钮（主进程 `applySetting` 已能正确响应批量 `change`）。
12. 未实现且刻意不做：屏幕录制/GIF（`PixMovie`）、公式识别（`PixFormulaRec`）、贴图动画（`PixLottie`）、取色面板完整 UI、账号/云同步/分享、右键 Shell 扩展、自动升级/崩溃上报。

---

## 10. 接手时的"坑"清单（改代码前必读）

- **不要给托盘菜单项加 `accelerator`**：全局热键已由 `globalShortcut` 注册，重复注册会失败并产生幽灵失败项。
- **不要把冻结帧改回 `pixclone://frame/…` 作为主路径**：自定义协议加载图像会污染 canvas，导出/OCR/取色会全废。
- **`nativeImage.getBitmap()` 在 Windows 上通常是 BGRA**：任何直接喂给 `encodePng`（RGBA）的位图必须先过 `frameFromImage()` 的通道判定，否则红蓝互换。
- **`getBitmapSize()` 与 `getSize()` 不同**：前者是设备像素。历史/Toast 里的尺寸展示用设备像素，窗口几何用 DIP，别混。
- **`desktopCapturer.getSources` 的 `thumbnailSize` 对所有 source 是同一个值**：不要假设每屏拿到的位图尺寸等于该屏设备像素。
- **PowerShell 里窗口句柄转换必须 `(ulong)(long)h`**，直接 `(ulong)$h` 在 64 位上会抛错。
- **`{index}` 在 `save.filenamePattern` 里不跨会话维护**：不要依赖它防覆盖，碰撞后缀逻辑才是防线。
- **`registerOne(name, '')` 是"清空"语义**，不是失败；改绑失败要回滚旧值。
- **`pins.pngOf()` 可能返回 `null`**，所有上层调用必须判空。
- **通道名在两侧硬编码**：主进程里直接写字符串的有 `settings:tab`（`settingsWindow.js`）、`pin:ready`、`ui:copyText`、`ui:sound`、`scroll:command`、`scroll:state`、`pin:close-all`；preload 侧硬编码了 `ui:copyText`。改一侧忘另一侧会静默失效（本轮 `ui:sound` 就是这么埋下的）。新通道一律加到 `defaults.js` 的 `IPC` 里。
  - 备注：`ipcMain.on('pin:close-all')` 目前没有渲染端调用方（见 §11.1）。
- **审计工具的 `res.name / res.text / res.message not exported` 是误报**（那是 `io.savePng()` 等返回对象的属性，不是模块导出缺失），别去"修"它。
- **`test/run-tests.js` 不需要 Electron**；不要在里面 `require('electron')`，否则测试在本环境彻底跑不动。

---

## 11. 速查表

### 默认热键（可在设置里改，改后即时重注册）
| 动作 | 默认 |
| --- | --- |
| 截图（选区+标注） | `Ctrl+1` |
| 快速截图（直接复制） | `Ctrl+2` |
| 整屏截图并贴图 | `Ctrl+3` |
| 显示/隐藏全部贴图 | `Ctrl+4` |
| 滚动长截图（Beta） | `Ctrl+5` |
| 剪贴板内容贴图 | `Ctrl+Shift+V` |
| 关闭全部贴图 | `Ctrl+Shift+Q` |

### 设置键（`src/common/defaults.js` → `DEFAULT_SETTINGS`，已逐项校对）
```
version(1)
capture.dimOpacity(0.45) capture.sound(true) capture.showMagnifier(true)
capture.snapToWindows(true) capture.afterSelectAction('annotate') capture.maxEdge(0 = 不限制)
save.dir('') save.format('png') save.copyAfterSave(false) save.filenamePattern('pixclone_{date}_{time}')
pin.alwaysOnTop(true) pin.opacity(1) pin.wheelZoom(true) pin.hideOnClickOutside(false) pin.maxPins(60)
scroll.intervalMs(420) scroll.maxFrames(80) scroll.maxHeightPx(24000)
ui.theme('dark') ui.language('zh-CN') ui.tray(true) ui.showGuide(true)
history.maxItems(200) system.autoStart(false) ocr.provider('auto') ocr.language('zh-Hans-CN')
hotkeys.* （见上表）
ui.settingsGeometry   （运行时写入，不在默认值里）
```

### IPC 通道（`defaults.js` 的 `IPC` 共 27 个）
```
overlay:init  overlay:ready  overlay:capture-region *  overlay:commit  overlay:cancel
overlay:list-windows  overlay:close *
pin:ready  pin:init  pin:state  pin:command
settings:get  settings:set  settings:setHotkey  settings:changed
history:list  history:open  history:clear  history:changed  history:delete
io:choose-folder  io:open-save-folder
ocr:image  ui:toast  ui:sound
scroll:state  scroll:command
（* = 已声明但全仓无引用，见 §11.1）
```
**不在 `defaults.js` 里、但实际在用的通道名（两侧硬编码字符串）：**
`settings:tab`（preload `CH.settingsTab` + `settingsWindow.js` 发送）、`ui:copyText`（preload 发送 + `main.js` 接收）、`pin:close-all`（仅 `main.js` 接收，无发送方）。
> 主进程硬编码接收/发送清单：`pin:ready`、`ui:copyText`、`ui:sound`、`scroll:command`、`scroll:state`、`settings:tab`、`pin:close-all`（见 §10）。

#### §11.1 已声明但未接通的通道 / 死处理器（别去追，要么接上要么删）
| 项 | 现状 |
| --- | --- |
| `IPC.overlayCaptureRegion` (`overlay:capture-region`) | 定义了但全仓无任何引用（主/渲染/preload 都没用） |
| `IPC.overlayCloseSelf` (`overlay:close`) | 同上；overlay 窗口关闭实际走主进程 `win.close()` |
| `ipcMain.on('pin:ready', …)` (`main.js` 第 141 行) | 渲染端从不发（pin 渲染只用 `onInit/onState/command`），且 `pins.command(…, 'hello')` 在 switch 里落 `default` 空操作 |
| `ipcMain.on('pin:close-all', …)` (`main.js` 第 150 行) | 渲染端无调用方（关闭全部贴图走热键/托盘 → `actions.closeAllPins()`） |
| `winspy.js` `BLACKLIST` 里的 `*.exe` 项 | 匹配的是 `cls`/`title`，进程名永不命中（见 §9 P1-5） |
| `Settings.reset()` (`settings.js` 第 64 行) | 方法完整（`structuredClone(DEFAULT_SETTINGS)` + `saveNow()` + 批量 `change`），但**没有 IPC 通道、也没有设置面板按钮**，目前用户无法恢复默认 |

处理建议：删掉前四个（或给它们真实用途：例如让 overlay 渲染端用 `overlay:close` 自己收尾、用 `pin:close-all` 做贴图右键“关闭全部”），不要为了“看起来完整”而保留。

### 关键常量 / 可调参数（已逐行校对源码）
```
stitch.js  findOverlap(a, b, opts): minOverlap(默认 12，下限 4) threshold(6.5) cols(48)
            从最大重叠向下搜索，首个 err <= threshold 即接受；否则 best <= threshold*3 记 weak
            采样 stepX = max(1, round(width / max(8, cols)))、stepY = max(1, round(overlap / 24))
            overlap >= b.height 则整帧 skipped；concat 时 padX = 0（左对齐归一化）
capture.js cleanTemp(dir, keepMs = 60_000) 只清 shot-*.png
            thumbnailSize = max(各屏 wanted 尺寸)（这就是共享尺寸问题根源，在 scroll.js 补偿）
            maxEdge 仅在 capture.maxEdge > 0 时生效（默认 0 = 不限制）
pins.js    最小 24 DIP；zoom 上限 8192 DIP；超出 workArea 等比缩小并钳到屏内
            opacity 0.15..1；超 pin.maxPins 时关掉最早创建的贴图；id = pin-<ts>-<seq>
history.js id = `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`，文件 <id>.png
io.js      非法字符 \/:*?"<>| → _，空白折叠；碰撞后缀 -2..-500
overlay    渲染端 8 秒未收到 init 帧 → 报错「未收到冻结画面数据」；冻结帧 60 秒后清理
```

---

## 12. 相关文档

- `README.md`：功能说明、按键表、贴图交互、PixPin 模块对照表、OCR 说明、测试覆盖、已知限制、目录结构。
- `docs/REA-FINDINGS.md`：PixPin 逆向证据（observed / derived / inferred 分级）、模块命名、日志与配置字段证据。
- 本文件：状态、决策、修复史、验证现状、待办、坑清单。
