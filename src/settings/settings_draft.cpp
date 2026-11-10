#include "settings_draft.h"
#include "settings_apply.h"
#include "ui/i18n.h"
#include "storage/image_export.h"
#include "storage/store.h"
#include <cmath>
#include <optional>

namespace still {
namespace {
QVariantList fields(int page) {
    QVariantList rows;
    auto section = [&](const QString &title) { rows.append(QVariantMap{{"type", "section"}, {"title", title}}); };
    auto add = [&](const QString &type, const QString &group, const QString &key, const QString &title,
                   const QString &description, const QVariant &value, const QVariantMap &extra = {}) {
        QVariantMap row{{"type", type}, {"group", group}, {"key", key}, {"title", title}, {"description", description}, {"defaultValue", value}};
        for (auto it = extra.begin(); it != extra.end(); ++it) row.insert(it.key(), it.value());
        rows.append(row);
    };
    if (page == 1) {
        section("界面");
        add("choice", "appearance", "language", "界面语言", "", "zh_CN",
            {{"options", QVariantList{QVariantMap{{"text", "简体中文"}, {"value", "zh_CN"}}, QVariantMap{{"text", "English"}, {"value", "en"}}}}});
        add("choice", "appearance", "theme", "外观", "", "system",
            {{"options", QVariantList{QVariantMap{{"text", "跟随系统"}, {"value", "system"}}, QVariantMap{{"text", "浅色"}, {"value", "light"}}, QVariantMap{{"text", "深色"}, {"value", "dark"}}}}});
        section("截图");
        add("choice", "capture", "afterSelectAction", "选区完成后", "按 Enter 时执行的操作", "annotate",
            {{"options", QVariantList{QVariantMap{{"text", "打开编辑器"}, {"value", "annotate"}}, QVariantMap{{"text", "复制图片"}, {"value", "copy"}}, QVariantMap{{"text", "贴图到屏幕"}, {"value", "pin"}}, QVariantMap{{"text", "保存图片"}, {"value", "save"}}, QVariantMap{{"text", "快速保存"}, {"value", "quickSave"}}}}});
        add("switch", "capture", "showMagnifier", "显示放大镜", "精确查看边缘和像素颜色", true);
        add("switch", "capture", "snapToWindows", "自动吸附窗口", "单击即可选中鼠标下的窗口", true);
        section("贴图");
        add("switch", "pin", "wheelZoom", "滚轮缩放", "以鼠标位置为中心调整贴图大小", true);
        add("switch", "pin", "hideOnClickOutside", "点击外部时隐藏", "可通过快捷键或托盘菜单重新显示", false);
        add("switch", "pin", "restoreOnStartup", "启动时恢复贴图", "恢复上次退出时未关闭的图片与布局", false);
        section("文件保存");
        add("directory", "save", "dir", "默认保存目录", "", "", {{"placeholder", "系统图片文件夹"}});
        add("template", "save", "nameTemplate", "文件名模板", "{date} 日期 · {time} 时间 · {ms} 毫秒；重名自动编号", defaultImageNameTemplate);
        add("choice", "save", "format", "默认图片格式", "用于快速保存和保存对话框的初始格式", "png",
            {{"options", QVariantList{QVariantMap{{"text", "PNG"}, {"value", "png"}}, QVariantMap{{"text", "JPEG"}, {"value", "jpeg"}}}}});
        add("number", "save", "jpegQuality", "JPEG 质量", "数值越高，画质越好、文件越大", 95, {{"minimum", 1}, {"maximum", 100}, {"visibleForFormat", "jpeg"}});
        add("number", "save", "pngCompression", "PNG 压缩", "始终无损；数值越高，文件越小、保存越慢", 50, {{"minimum", 0}, {"maximum", 100}, {"visibleForFormat", "png"}});
        add("switch", "save", "copyAfterSave", "保存后复制图片", "保存到文件后，也放入剪贴板", false);
        add("switch", "save", "copyAsFile", "复制为图像文件", "使用当前保存格式；临时文件由系统清理", false);
    } else if (page == 2) {
        section("全局快捷键");
        for (int i = 0; i < hotkeyNames.size(); ++i) add("shortcut", "hotkeys", hotkeyNames[i], hotkeyLabels[i], "", "");
    } else if (page == 3) {
        section("保存与空间");
        add("switch", "history", "enabled", "自动保留截图历史", "关闭后仍可复制、贴图和手动保存", true);
        add("directory", "history", "dir", "历史保存目录", "选择专用空文件夹，确认目录后自动迁移旧历史", "", {{"placeholder", "应用数据目录 / History"}});
        add("number", "history", "maxItems", "保留历史数量", "超出数量或容量后清理最早的截图", 200, {{"minimum", 1}, {"maximum", 5000}});
        add("number", "history", "maxMiB", "历史容量上限", "默认 1024 MiB，约 1 GiB", 1024, {{"minimum", 1}, {"maximum", 102400}, {"unit", "MiB"}});
    } else if (page == 4) {
        section("文字识别");
        add("text", "ocr", "language", "识别语言", "如 zh-Hans-CN 或 en-US，需系统支持对应语言", "zh-Hans-CN");
        section("贴图与历史");
        add("switch", "pin", "alwaysOnTop", "新贴图保持置顶", "", true);
        add("number", "pin", "maxPins", "最多贴图数", "超出后关闭最早创建的贴图", 60, {{"minimum", 1}, {"maximum", 200}});
        section("滚动长截图");
        add("number", "scroll", "intervalMs", "采样间隔", "请缓慢滚动，并避开固定表头和动画", 420, {{"minimum", 200}, {"maximum", 5000}, {"unit", "ms"}});
    } else if (page == 5) {
        section("记录与使用");
        add("switch", "clipboard", "enabled", "记录剪贴板历史", "文字、图片、文件路径与原始格式仅保存在本机", true);
        add("switch", "clipboard", "pasteOnSelect", "选择后直接粘贴", "回到之前的窗口；Ctrl+Enter 始终仅复制", true);
        section("本地保存");
        add("directory", "clipboard", "dir", "剪贴板保存目录", "独立于截图历史；改到其他磁盘后会迁移原有内容", "", {{"placeholder", "应用数据目录 / Clipboard"}});
        add("number", "clipboard", "maxItems", "保留条目数量", "优先清理最早的非固定条目", 200, {{"minimum", 1}, {"maximum", 2000}});
        add("number", "clipboard", "maxMiB", "剪贴板容量上限", "固定条目不自动删除，容量不足时停止新增", 256, {{"minimum", 1}, {"maximum", 10240}, {"unit", "MiB"}});
        add("folder", "clipboard", "", "查看本地数据", "文件条目只记录路径，不备份文件本体；历史重启后保留", "");
    }
    for (auto &value : rows) {
        auto row = value.toMap();
        for (const auto &key : {"title", "description", "placeholder"}) if (row.contains(key)) row[key] = t(row[key].toString());
        auto options = row.value("options").toList();
        for (auto &option : options) { auto item = option.toMap(); item["text"] = t(item["text"].toString()); option = item; }
        if (!options.isEmpty()) row["options"] = options;
        value = row;
    }
    return rows;
}
QVariantMap field(const QString &group, const QString &key) {
    for (int page = 1; page <= 5; ++page) for (const auto &value : fields(page)) {
        const auto row = value.toMap();
        if (!key.isEmpty() && row.value("group") == group && row.value("key") == key) return row;
    }
    return {};
}
std::optional<QJsonValue> settingValue(const QVariantMap &row, const QVariant &value) {
    const QString type = row.value("type").toString();
    if (type == "number") {
        bool ok = false; const double number = value.toDouble(&ok);
        if (!ok || value.metaType().id() == QMetaType::Bool || !std::isfinite(number) || number != std::floor(number) || number < row.value("minimum").toInt() || number > row.value("maximum").toInt()) return std::nullopt;
        return QJsonValue(int(number));
    }
    if (type == "switch") {
        if (value.metaType().id() != QMetaType::Bool) return std::nullopt;
        return QJsonValue(value.toBool());
    }
    if (value.metaType().id() != QMetaType::QString) return std::nullopt;
    const QString text = value.toString().trimmed();
    if (type == "template" && !validImageNameTemplate(text)) return std::nullopt;
    if (type == "choice") {
        bool found = false; for (const auto &option : row.value("options").toList()) if (option.toMap().value("value") == text) found = true;
        if (!found) return std::nullopt;
    }
    return QJsonValue(text);
}
QJsonValue actualDefault(const QVariantMap &row) {
    return row.value("type") == "shortcut" ? defaultHotkeys().value(row.value("key").toString())
                                           : QJsonValue::fromVariant(row.value("defaultValue"));
}
} // namespace

QJsonObject normalizeSettings(QJsonObject form) {
    for (int page = 1; page <= 5; ++page) for (const auto &value : fields(page)) {
        const auto row = value.toMap(); const QString key = row.value("key").toString();
        if (key.isEmpty()) continue;
        const QString group = row.value("group").toString(); auto object = form.value(group).toObject();
        const auto valid = settingValue(row, object.value(key).toVariant());
        object[key] = valid.value_or(QJsonValue::fromVariant(row.value("defaultValue")));
        form[group] = object;
    }
    return form;
}
SettingsDraft::SettingsDraft(SettingsApply &settings, QObject *parent)
    : QObject(parent), settings_(settings), form_(normalizeSettings(settings.settings())), accepted_(form_) {
    connect(&settings_, &SettingsApply::finished, this, [this](bool success, const QJsonObject &saved, const QStringList &failures, const QString &message) {
        if (settings_.busy()) { emit statusChanged(); return; }
        const auto effective = normalizeSettings(saved);
        // Preserve unfinished input and newer edits while an older save completes or rolls back.
        for (auto group = effective.begin(); group != effective.end(); ++group) {
            auto visible = form_.value(group.key()).toObject();
            if (!group.value().isObject()) { form_[group.key()] = group.value(); continue; }
            const auto requested = submitted_.value(group.key()).toObject();
            const auto previous = accepted_.value(group.key()).toObject();
            const auto fields = group.value().toObject();
            for (auto field = fields.begin(); field != fields.end(); ++field) {
                const auto expected = requested.contains(field.key()) ? requested.value(field.key()) : previous.value(field.key());
                if (visible.value(field.key()) == expected) visible[field.key()] = field.value();
            }
            form_[group.key()] = visible;
        }
        accepted_ = effective;
        submitted_ = {};
        validationErrorField_.clear();
        statusError_ = !success || !failures.isEmpty() || !message.isEmpty();
        status_ = !success ? t("更改未生效，已恢复原设置：") + message
                          : failures.isEmpty() ? message : t("快捷键冲突，已保留原值：") + failures.join(QStringLiteral("、"));
        if (success && !failures.isEmpty() && !message.isEmpty()) status_ += QStringLiteral("；") + message;
        emit formChanged(); emit statusChanged();
    });
    if (!settings_.failedHotkeys().isEmpty()) { statusError_ = true; status_ = t("快捷键未注册：") + settings_.failedHotkeys().join(QStringLiteral("、")); }
    connect(&InterfacePreferences::instance(), &InterfacePreferences::changed, this, [this] {
        status_ = InterfacePreferences::instance().translatedCaption(status_);
        emit statusChanged();
    });
}

QVariantList SettingsDraft::rows(int page) {
    auto rows = fields(page);
    for (auto &value : rows) {
        auto row = value.toMap();
        if (!row.value("key").toString().isEmpty()) row["defaultValue"] = actualDefault(row).toVariant();
        value = row;
    }
    return rows;
}

void SettingsDraft::reloadSaved() {
    form_ = normalizeSettings(settings_.settings()); accepted_ = form_; submitted_ = {};
    emit formChanged();
}

void SettingsDraft::setDraft(const QString &group, const QString &key, const QVariant &value) {
    if (field(group, key).isEmpty()) return;
    auto object = form_.value(group).toObject(); object[key] = QJsonValue::fromVariant(value); form_[group] = object;
    emit formChanged();
}
void SettingsDraft::setSetting(const QString &group, const QString &key, const QVariant &value) {
    const auto row = field(group, key); if (row.isEmpty()) return;
    const auto valid = settingValue(row, value);
    if (row.value("type") == "directory" && valid && !valid->toString().isEmpty() && !isAbsoluteDirectoryPath(valid->toString())) {
        status_ = t("目录必须使用完整路径，或留空使用默认目录。");
        validationErrorField_ = group + '.' + key;
        statusError_ = true; emit statusChanged(); return;
    }
    if (!valid) {
        if (row.value("type") == "template") {
            status_ = t("文件名无效：请使用普通文字和 {date}、{time}、{ms}，不要包含路径或特殊字符。");
            validationErrorField_ = group + '.' + key;
            statusError_ = true; emit statusChanged();
        }
        return;
    }
    auto visible = form_.value(group).toObject(); visible[key] = *valid; form_[group] = visible;
    emit formChanged();
    auto requested = submitted_.value(group).toObject();
    if (*valid == (requested.contains(key) ? requested.value(key) : accepted_.value(group).toObject().value(key))) {
        if (validationErrorField_ == group + '.' + key) {
            validationErrorField_.clear(); statusError_ = false;
            status_ = settings_.busy() ? t("正在保存…") : QString();
            emit statusChanged();
        }
        return;
    }
    requested[key] = *valid; submitted_[group] = requested;
    validationErrorField_.clear();
    status_ = t("正在保存…"); statusError_ = false;
    settings_.apply({{group, QJsonObject{{key, *valid}}}});
    emit statusChanged();
}
void SettingsDraft::restoreDefault(const QString &group, const QString &key) {
    const auto row = field(group, key);
    if (!row.isEmpty()) setSetting(group, key, actualDefault(row).toVariant());
}
void SettingsDraft::commitDrafts() {
    const auto draft = form_;
    for (int page = 1; page <= 5; ++page) for (const auto &value : fields(page)) {
        const auto row = value.toMap(); const QString group = row.value("group").toString(), key = row.value("key").toString();
        const auto requested = submitted_.value(group).toObject();
        const auto expected = requested.contains(key) ? requested.value(key) : accepted_.value(group).toObject().value(key);
        const auto draftValue = draft.value(group).toObject().value(key);
        if (!key.isEmpty() && draftValue != expected) setSetting(group, key, draftValue.toVariant());
    }
}
} // namespace still
