#include "ui/i18n.h"
#include "clipboard_data.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QTextDocumentFragment>
#include <QtEndian>
#include <algorithm>

namespace still {
namespace {
constexpr qint64 maxBytes = 32LL * 1024 * 1024;
constexpr qint64 maxImagePixels = 32LL * 1024 * 1024;
// Accepted images must fit the same pixel budget when restored, regardless of their original pixel format.
bool validImageSize(const QSize &size) { return size.isValid() && qint64(size.width()) * size.height() <= maxImagePixels; }
bool virtualFile(const QString &name) {
    return name.contains("FileContents", Qt::CaseInsensitive) || name.contains("FileGroupDescriptor", Qt::CaseInsensitive);
}
bool transientFormat(const QString &name) {
    return name.startsWith("application/x-still-") || name.startsWith("application/x-pixclone-")
        || name.contains("Ole Private Data", Qt::CaseInsensitive)
        || name.contains("DataObject", Qt::CaseInsensitive) || name == "application/x-qt-image";
}
bool copyEffect(const QString &name) {
    return name.contains("DropEffect", Qt::CaseInsensitive) || name == "x-special/gnome-copied-files";
}
QByteArray formatBytes(const ClipPayload &payload, const QString &name) {
    for (const auto &format : payload.formats) if (format.name == name) return format.data;
    return {};
}
} // namespace

qint64 ClipPayload::bytes() const {
    qint64 result = image.sizeInBytes();
    for (const auto &format : formats) result += format.data.size();
    return result;
}

bool excludedClipboard(const QMimeData &mime) {
    for (const auto &name : mime.formats()) {
        if (name == "x-kde-passwordManagerHint" || name.contains("ExcludeClipboardContentFromMonitorProcessing", Qt::CaseInsensitive)
            || name.contains("org.nspasteboard.ConcealedType", Qt::CaseInsensitive)
            || name.contains("org.nspasteboard.TransientType", Qt::CaseInsensitive)) return true;
        if (name.contains("CanIncludeInClipboardHistory", Qt::CaseInsensitive)) {
            const auto value = mime.data(name);
            if (value.size() >= 4 && qFromLittleEndian<quint32>(value.constData()) == 0) return true;
        }
    }
    return false;
}

ClipPayload captureClipboard(const QMimeData &mime, QString *error) {
    ClipPayload result;
    if (excludedClipboard(mime)) return result;
    if (mime.hasImage()) {
        const auto value = mime.imageData();
        result.image = qvariant_cast<QImage>(value);
        if (result.image.isNull() && value.canConvert<QPixmap>()) result.image = qvariant_cast<QPixmap>(value).toImage();
        if (!result.image.isNull() && (!validImageSize(result.image.size()) || result.image.sizeInBytes() > 128LL * 1024 * 1024)) {
            if (error) *error = t("图片过大，本次未加入剪贴板历史");
            return {};
        }
        result.image.setDevicePixelRatio(1);
    }
    result.text = mime.hasText() ? mime.text() : QString();
    result.urls = mime.hasUrls() ? mime.urls() : QList<QUrl>();
    qint64 bytes = 0;
    const auto names = mime.formats();
    for (const auto &name : names.mid(0, 60)) {
        if (name.size() > 256 || transientFormat(name) || virtualFile(name) || copyEffect(name)) continue;
        if (!result.image.isNull() && name.startsWith("image/") && name != "image/svg+xml") continue;
        if (name == "text/uri-list" || name == "text/plain") continue;
        const QByteArray value = mime.data(name);
        bytes += value.size();
        if (bytes > maxBytes) { if (error) *error = t("剪贴板内容超过 32 MiB，本次未记录"); return {}; }
        if (!value.isEmpty()) result.formats.append({name, value});
    }
    if (!result.text.isEmpty()) result.formats.append({"text/plain", result.text.toUtf8()});
    if (!result.urls.isEmpty()) {
        QByteArray urls;
        for (const auto &url : result.urls) urls += url.toEncoded() + "\r\n";
        result.formats.append({"text/uri-list", urls});
    }
    if (result.bytes() - result.image.sizeInBytes() > maxBytes) {
        if (error) *error = t("剪贴板内容超过 32 MiB，本次未记录");
        return {};
    }
    if (result.empty() && std::any_of(names.cbegin(), names.cend(), virtualFile) && error)
        *error = t("虚拟附件请先保存为本地文件，再复制到历史");
    return result;
}

QMimeData *restoreClipboard(const ClipPayload &payload) {
    auto *mime = new QMimeData;
    for (const auto &format : payload.formats) if (!copyEffect(format.name)) mime->setData(format.name, format.data);
    if (!payload.text.isEmpty()) mime->setText(payload.text);
    if (!payload.urls.isEmpty()) {
        mime->setUrls(payload.urls);
#ifdef Q_OS_WIN
        // Replaying an old cut operation must not unexpectedly move the original files.
        mime->setData("application/x-qt-windows-mime;value=\"Preferred DropEffect\"", QByteArray::fromHex("01000000"));
#endif
    }
    if (!payload.image.isNull()) mime->setImageData(payload.image);
    return mime;
}

QByteArray encodeClipboard(ClipPayload payload, ClipEntry *entry, QString *error) {
    if (!payload.image.isNull()) {
        if (!validImageSize(payload.image.size())) { *error = t("图片尺寸超过历史读取上限，本次未记录"); return {}; }
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!payload.image.save(&buffer, "PNG")) { *error = t("图片编码失败"); return {}; }
        payload.formats.append({"image/png", png});
        entry->thumbnail = payload.image.scaled(128, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        // Keep the existing index byte budget: noisy previews shrink instead of growing storage.
        while (!entry->thumbnail.isNull()) {
            QByteArray thumbnail;
            QBuffer buffer(&thumbnail); buffer.open(QIODevice::WriteOnly);
            if (!entry->thumbnail.save(&buffer, "PNG")) { entry->thumbnail = {}; break; }
            if (thumbnail.size() <= 17000 || entry->thumbnail.width() <= 64) break;
            entry->thumbnail = entry->thumbnail.scaled(entry->thumbnail.size() * 0.75, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    if (payload.text.isEmpty()) {
        const auto html = formatBytes(payload, "text/html");
        if (!html.isEmpty()) payload.text = QTextDocumentFragment::fromHtml(QString::fromUtf8(html)).toPlainText();
    }
    bool files = !payload.urls.isEmpty();
    for (const auto &url : payload.urls) if (!url.isLocalFile()) files = false;
    entry->kind = files ? "files" : !payload.image.isNull() ? "image" : !formatBytes(payload, "text/html").isEmpty() ? "rich" : !payload.urls.isEmpty() ? "link" : !payload.text.isEmpty() ? "text" : "data";
    entry->preview = payload.text.left(4096);
    if (files) {
        QStringList paths;
        for (const auto &url : payload.urls) paths.append(url.toLocalFile());
        entry->preview = paths.join('\n').left(4096);
        entry->title = paths.size() == 1 ? QFileInfo(paths.first()).fileName() : QStringLiteral("%1 个文件").arg(paths.size());
    } else if (entry->kind == "image") entry->title = QStringLiteral("图片 · %1 × %2").arg(payload.image.width()).arg(payload.image.height());
    else entry->title = payload.text.simplified().left(160);
    if (entry->title.isEmpty()) entry->title = !payload.urls.isEmpty() ? payload.urls.first().toDisplayString().left(160) : QStringLiteral("其他格式内容");
    QJsonArray formats;
    qint64 total = 0;
    for (const auto &format : payload.formats) {
        total += format.data.size();
        if (total > maxBytes) { *error = t("编码后的内容超过 32 MiB，本次未记录"); return {}; }
        entry->formats.append(format.name);
        formats.append(QJsonObject{{"mime", format.name}, {"data", QString::fromLatin1(format.data.toBase64())}});
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"formats", formats}}).toJson(QJsonDocument::Compact);
    entry->bytes = bytes.size();
    return bytes;
}

ClipPayload decodeClipboard(const QByteArray &bytes, QString *error) {
    ClipPayload result;
    if (bytes.size() > 48LL * 1024 * 1024) { *error = t("剪贴板历史文件过大"); return {}; }
    const auto object = QJsonDocument::fromJson(bytes).object();
    const auto formats = object.value("formats").toArray();
    if (object.value("version").toInt() != 1 || formats.isEmpty() || formats.size() > 64) { *error = t("剪贴板历史格式无效"); return {}; }
    qint64 total = 0;
    for (const auto &value : formats) {
        const auto format = value.toObject();
        const QString name = format.value("mime").toString();
        const QByteArray data = QByteArray::fromBase64(format.value("data").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        total += data.size();
        if (name.isEmpty() || name.size() > 256 || data.isEmpty() || total > maxBytes || transientFormat(name) || virtualFile(name)) { *error = t("剪贴板历史内容无效"); return {}; }
        result.formats.append({name, data});
    }
    result.text = QString::fromUtf8(formatBytes(result, "text/plain"));
    if (result.text.isEmpty()) {
        const auto html = formatBytes(result, "text/html");
        if (!html.isEmpty()) result.text = QTextDocumentFragment::fromHtml(QString::fromUtf8(html)).toPlainText();
    }
    const auto urls = formatBytes(result, "text/uri-list");
    for (const auto &line : urls.split('\n')) if (!line.trimmed().isEmpty()) result.urls.append(QUrl::fromEncoded(line.trimmed()));
    QByteArray png = formatBytes(result, "image/png");
    if (!png.isEmpty()) {
        QBuffer buffer(&png); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "PNG");
        const QSize size = reader.size();
        if (!validImageSize(size)) { *error = t("图片尺寸超过历史读取上限"); return {}; }
        result.image = reader.read();
        if (result.image.isNull()) { *error = t("历史图片已损坏"); return {}; }
    }
    return result;
}

QString missingClipboardFiles(const ClipPayload &payload) {
    QStringList missing;
    for (const auto &url : payload.urls) if (url.isLocalFile()) {
        const QString path = url.toLocalFile();
        if (path.startsWith("//") || path.startsWith("\\\\")) continue;
        if (!QFileInfo::exists(path)) missing.append(QFileInfo(path).fileName());
    }
    return missing.isEmpty() ? QString() : t("文件已移动或删除：") + missing.mid(0, 3).join(QStringLiteral("、"));
}

} // namespace still
