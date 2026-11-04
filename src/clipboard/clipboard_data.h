#pragma once

#include <QDateTime>
#include <QImage>
#include <QMimeData>
#include <QVector>
#include <QUrl>

namespace pixclone {

struct ClipFormat { QString name; QByteArray data; };
struct ClipPayload {
    QVector<ClipFormat> formats;
    QImage image;
    QString text;
    QList<QUrl> urls;
    qint64 bytes() const;
    bool empty() const { return formats.isEmpty() && image.isNull() && urls.isEmpty(); }
};

struct ClipEntry {
    QString id;
    QString kind;
    QString title;
    QString preview;
    QStringList formats;
    QDateTime created;
    qint64 bytes = 0;
    bool pinned = false;
    QImage thumbnail;
};

bool excludedClipboard(const QMimeData &mime);
ClipPayload captureClipboard(const QMimeData &mime, QString *error = nullptr);
QMimeData *restoreClipboard(const ClipPayload &payload);
QByteArray encodeClipboard(ClipPayload payload, ClipEntry *entry, QString *error);
ClipPayload decodeClipboard(const QByteArray &bytes, QString *error);
QString missingClipboardFiles(const ClipPayload &payload);

} // namespace pixclone
