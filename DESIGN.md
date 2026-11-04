---
name: "拾影 / Still"
description: "银白、石墨与克制玻璃层次的本地截图和剪贴板工具"
colors:
  accent: "#454b57"
  focus: "#454b57"
  primary: "#454b57"
  primary-ink: "#ffffff"
  primary-hover: "#353b46"
  primary-pressed: "#272d37"
  surface: "#ffffff"
  sidebar: "#e9ebef"
  nav-ink: "#242731"
  nav-muted: "#626875"
  nav-selected: "#ffffff"
  canvas: "#f4f5f7"
  ink: "#242731"
  muted: "#626875"
  selection: "#e8ebf0"
  selected-muted: "#525d70"
  border: "#d9dde5"
  hover: "#e2e6ed"
  glass: "rgba(247, 248, 251, 0.741176)"
  glass-rim: "rgba(255, 255, 255, 0.4)"
  navigation-glass: "rgba(248, 250, 252, 0.768627)"
  switch-off: "#8b94a3"
  error: "#b42318"
  dark-surface: "#1c1c1c"
  dark-sidebar: "#191919"
  dark-nav-ink: "#ededed"
  dark-nav-muted: "#adadad"
  dark-nav-selected: "#292929"
  dark-canvas: "#181818"
  dark-ink: "#ededed"
  dark-muted: "#adadad"
  dark-focus: "#a8a8a8"
  dark-primary: "#343434"
  dark-primary-hover: "#404040"
  dark-primary-pressed: "#242424"
  dark-switch-off: "#404040"
  dark-selection: "#292929"
  dark-selected-muted: "#c4c4c4"
  dark-border: "#383838"
  dark-hover: "#242424"
  dark-glass: "#242424"
  dark-glass-rim: "rgba(255, 255, 255, 0.094118)"
  dark-navigation-glass: "rgba(25, 25, 25, 0.8)"
  dark-error: "#ff9b90"
typography:
  title:
    fontFamily: "Microsoft YaHei UI"
    fontSize: "23px"
    fontWeight: 600
  body:
    fontFamily: "Microsoft YaHei UI"
    fontSize: "14px"
    fontWeight: 400
  item-title:
    fontSize: "15px"
    fontWeight: 500
  preview:
    fontSize: "15px"
    fontWeight: 400
  snippet:
    fontSize: "12px"
    fontWeight: 400
  control:
    fontSize: "13px"
    fontWeight: 400
  description:
    fontSize: "12px"
    fontWeight: 400
  section:
    fontSize: "13px"
    fontWeight: 600
  hint:
    fontSize: "11px"
    fontWeight: 400
rounded:
  field: "9px"
  button: "18px"
  search: "20px"
  navigation: "6px"
  clipboard-item: "4px"
  menu-item: "6px"
  filter: "8px"
  history-item: "12px"
  appearance: "10px"
  switch: "12px"
  switch-knob: "9px"
spacing:
  small: "4px"
  action-gap: "6px"
  content-gap: "12px"
  preview-gap: "16px"
  section-gap: "18px"
  row-gap: "22px"
  workspace-inset: "24px"
  workspace-top: "12px"
  preview-inset: "12px"
components:
  button:
    backgroundColor: "{colors.glass}"
    textColor: "{colors.ink}"
    typography: "{typography.control}"
    rounded: "{rounded.button}"
    height: "36px"
  button-primary:
    backgroundColor: "{colors.primary}"
    textColor: "{colors.primary-ink}"
    rounded: "{rounded.button}"
    height: "36px"
  button-primary-hover:
    backgroundColor: "{colors.primary-hover}"
  button-primary-pressed:
    backgroundColor: "{colors.primary-pressed}"
  button-selected:
    backgroundColor: "{colors.selection}"
    textColor: "{colors.focus}"
  field:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    typography: "{typography.control}"
    rounded: "{rounded.field}"
    height: "36px"
  search:
    backgroundColor: "{colors.glass}"
    rounded: "{rounded.search}"
    height: "40px"
  navigation-item:
    rounded: "{rounded.navigation}"
    height: "42px"
  filter:
    textColor: "{colors.muted}"
    rounded: "{rounded.filter}"
    height: "32px"
  switch:
    backgroundColor: "{colors.switch-off}"
    rounded: "{rounded.switch}"
    width: "38px"
    height: "24px"
  switch-checked:
    backgroundColor: "{colors.accent}"
  clipboard-item:
    rounded: "{rounded.clipboard-item}"
    height: "78px"
  clipboard-selected:
    backgroundColor: "{colors.selection}"
    textColor: "{colors.ink}"
  clipboard-preview:
    width: "320px"
  clipboard-menu:
    backgroundColor: "{colors.surface}"
    rounded: "{rounded.field}"
    padding: "6px"
    width: "232px"
  history-item:
    backgroundColor: "{colors.surface}"
    rounded: "{rounded.history-item}"
    height: "184px"
---

# Design System: 拾影 / Still

## Overview

**Creative North Star: "一整面的银色玻璃工作台"**

浅色使用银白、冷灰和轻玻璃。深色以中性黑灰和实色控件减少视觉干扰。侧栏与内容连成连续工作面，真实文字、图片和文件占据主体。

本规范记录实际 Qt Quick/QML 与 Widgets 实现，`Theme.qml` 是共享颜色来源。Windows 支持时使用 DWM 原生 Acrylic 实时背景模糊，失败或高对比度模式使用不透明回退；不以桌面截图伪造背景，不宣称实现 Apple 专有折射效果。

**Key Characteristics:**

- 连续的侧栏与内容工作面，无两块独立的大圆角外框。
- 六页共用紧凑页头；搜索和主要复制／粘贴动作位于顶部。
- 剪贴板以单列内容列表扫读，历史保留缩略图画廊，预览按需打开。
- 设置自动保存；中文 / English 与 Light / Dark / System 即时应用。
- 项目自绘取景框与一页标志保留轮廓，使用石墨底和银色角。

## Colors

frontmatter 记录复用的日间与夜间角色，`dark-` 前缀对应夜间。深色用浅灰文字、低对比选中底与清楚的键盘焦点区分层级。QML 八位色采用 `#AARRGGBB`，frontmatter 将其等价写为可移植 CSS `rgba()`。

### Primary

- 石墨 `primary` 用于粘贴、继续编辑等主要动作。深色的侧栏截图入口使用轻量图标和文字。
- `accent` 用于活动图标、开关和文字选择；夜间 `focus` 使用银色，保证键盘状态可辨认。
- `selection` 与 `selected-muted` 区分选中内容，导航另用 `nav-selected` 的小圆角色面。
- `error` 配合具体文字表达失败、冲突与迁移警告。

### Neutral

- `surface` 承载内容和菜单，`canvas` 承载窗口工作面，`sidebar` 为不透明回退导航底；玻璃可用时侧栏使用 `navigation-glass`。
- 浅色的 `glass` 与 `glass-rim` 提供半透明操作层和单层高光，深色控件采用实色。`ink`、`muted` 与导航辅助色负责阅读层级，`border`、`hover` 表达边界和交互。
- 共享控件禁用透明度为（0.45）。

**The Content Color Rule.** 外观切换更新界面、Qt 调色板、编辑器画布与工具栏；截图和贴图图像本身保持原色。

## Typography

Windows 主窗口与关闭贴图提示使用 `Microsoft YaHei UI`；其他平台指定 `Helvetica Neue`，尚未在 Mac 验证。不加载字体文件；Widgets 中仍采用 `Segoe UI` 的工具栏通过 Qt Han 回退使用中文 UI 字体。frontmatter 字体描述 Windows 主界面，不宣称全平台使用相同字体。

页面标题为 title，条目首个非空行为 item-title；后续非空行保留换行，最多显示两行 snippet。文件辅助信息缩短为父目录，完整路径保留在预览和可访问描述。页名、计数和设置状态同行；正文、说明和时间尺寸见 frontmatter。行高和字距沿用 Qt 字体度量，用户内容与文件名不翻译。

## Layout

尺寸均为 Qt 逻辑像素。主窗口默认（1080×740），最小（800×620）；侧栏固定（176px），上方窗口栏（48px）。所有六页统一横向（24px）、顶部（12px）、页头（40px）与正文间距（12px），不为设置页重复增加大段顶部留白。侧栏内容距窗口栏（18px），左右（12px），底部（20px）；导航高（42px）、间距（6px）。

内容导航为剪贴板、已固定、截图历史；底部偏好入口切换为四类偏好，“返回内容”置于分类上方。内容页右下角使用（44×44px）圆形截图浮钮，列表末尾留出避让空间。偏好页隐藏截图按钮。暂停记录靠侧栏底部。Windows 集成真实最小化、最大化／还原、关闭、标题拖动和边缘缩放，其他平台保留原生装饰。

剪贴板页名与计数同行，搜索高（40px）、宽不超过（360px）。顶部保留直接筛选和更多，只有可粘贴到原窗口时显示粘贴。预览位于更多菜单。内容宽小于（660px）时筛选左右内边距收至（8px，否则12px），预览不可用并隐藏 Enter 提示。

单列行高（78px）、行距（4px）、上下 margin（2px）、宽为列表宽减（4px）；图片槽（64px）内图像最多（60×52px），类型槽（28px）内图标（20px）。按需预览宽为内容宽的（40%）且最多（320px），以（1px）竖线分隔，横向间距（16px）；预览透明，内部左（12px）、右（4px）、上下（6px）。

历史画廊按（224px）分列、单元高（198px）、卡片扣除（14px）间距、内边距（12px）；存储弹层上限（580×560px）。设置仅纵向滚动，普通行最小高（50px）、有说明（68px）、外观行（116px）；分节为（18px 留白＋1px 线＋18px 留白），自动保存状态与标题同行、最大宽（260px）。

滚动截图继续使用（42px）工具条、（32px）按钮和（20px）图标；固定比例预览图宽最多（160px），另有（8px）滚动条，视口最高（360px）。新主窗口方向不改变这些已建立的交互尺寸。

**The Content First Rule.** 首页优先展示可辨认的内容对象，偏好、预览和低频操作按需出现。

## Elevation & Depth

深度来自真实系统 Acrylic、克制的半透明着色和单层细高光。`GlassSurface` 使用顶端轻亮、中段原色、底端稍暗的细微纵向渐变和（1px）rim，无额外阴影、重复内框或装饰动画；主要按钮只略微增强高光。侧栏和内容区本身不套 `GlassSurface` 大外框。高对比度或系统不支持时保留相同结构与清楚的实色背景。

**The Continuous Surface Rule.** 以内容和留白组织主窗口；玻璃表达操作层，不把侧栏、列表和预览拆成一块块圆角容器。

## Shapes

普通及主要按钮按自身高度的一半形成胶囊；frontmatter 的 button 对应默认高度。搜索圆润，导航仅小圆角软色面，剪贴板行仅轻微圆角，字段和菜单保持小圆角。列表、预览和设置不包外层卡片；历史对象仍保留缩略图卡片和（2px）选中边。窗口非最大化外缘保留（12px）系统轮廓，最大化归零，这不等于两个独立工作区外框。

## Components

### Buttons and Fields

`UiButton` 主动作使用石墨胶囊，普通按钮为轻玻璃，quiet 静态透明；焦点采用 `Theme.focus` 细边。图标槽（18×18px）、tooltip 延迟（600ms），保留当前语言动作名与可访问名称。字段、下拉与数字控件沿用共享实现，输入高（36px）；搜索使用独立玻璃背景。开关轨道（38×24px）、滑块（18×18px），键盘焦点同时显示轨道与外层轮廓。

### Navigation and Settings

导航支持鼠标和上下方向键，切换后保留焦点。语言两项直接选择、外观三张微缩示意，保留 RadioButton 语义。选项立即提交；文字、路径与数字完成输入、切页、关闭或存储弹层关闭时提交；后台保留最新候选，失败恢复未被新输入替换的原值，无保存按钮。

### Clipboard List and Preview

标题与两行摘要清楚分层，时间右对齐。选中或悬停时显示复制、固定和删除三个居中按钮。已固定条目的固定按钮始终可见。列表上下与左右逐项选择，搜索上下逐项。Ctrl+C、Delete、空格、右键和三种 Enter 动作保持。

预览默认关闭，打开后延迟读取选中内容，离页、隐藏或销毁取消过期读取。文字只读可选择，图片按设备像素比等比显示；缩略图不超过（128×96px）、PNG（17000 bytes）、base64 小于（24000 字符），索引边界（2000 条／64MiB）；原始内容不改写。`UiImages` 只持有程序图标、缩略图及一个选中原图。

更多／右键菜单使用 `Popup.Item`，主题底、细边、统一（36px）菜单项与（8px）窗口边界，保留 Qt 默认 Esc／外点关闭行为。粘贴目标仅在明确从外部打开时记录，失焦丢弃旧目标。清空仍需确认。

### Pin Close Prompt

贴图附近显示（300px）非模态小浮层，采用 `Qt::Popup`、`show()`、无嵌套事件循环，不显示或抬高主窗口。默认取消；取消、Esc、外点均不保存免提示，只在勾选并确认关闭时保存。复选框保留 Qt 命中与文字，自绘勾选标记，避免 Windows 半透明浮层的原生勾消失。浮层使用主题实色底、小圆角和石墨确认动作，不宣称该 Widgets 浮层具有 DWM 模糊。

### History and Identity

历史缩略图保持比例，继续编辑、贴图、删除、清空、目录与分批加载保持；存储状态在当前弹层可见。面向用户统一“拾影 / Still”，内部 PixClone target、URI、组织、数据目录和实例标识保持兼容。

标志源为 `assets/still.svg`，项目自绘几何取景框与一页，保留轮廓，石墨底、白色主体、银色角。`scripts/generate-logo.ps1` 生成 PNG、七尺寸 ICO 与 ICNS；出处在 `assets/still.png.json`。验证方法见 `README.md`。

## Do's and Don'ts

### Do:

- **Do** 保持银白／石墨与夜间深石墨／银色焦点的角色关系。
- **Do** 保持连续工作面、紧凑页头、顶部复制动作和克制的单层玻璃高光。
- **Do** 让设置自动保存，保留键盘、焦点、禁用和当前语言可访问名称。
- **Do** 记录真实系统合成与结构抓取的不同证据范围，保留不透明回退。

### Don't:

- **Don't** 恢复旧鼠尾草／松绿规范、蓝色主视觉或剪贴板大卡片网格。
- **Don't** 给侧栏、列表和预览增加独立的大圆角玻璃外框或重复高光内框。
- **Don't** 在目标不可靠时复用旧粘贴目标，或让关闭贴图提示抬高主窗口。
- **Don't** 将合成测试图当发布资源，将结构抓取当真实 DWM 输出，或把历史审阅等同于本轮和其他平台验收。
