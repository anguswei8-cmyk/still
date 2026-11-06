#include "image_copy.h"
#include "clipboard_data.h"
#include "storage/image_export.h"

#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QTemporaryDir>

namespace still {

bool copyImageToClipboard(const QImage &image, const QJsonObject &saveOptions, QString *error) {
    ClipPayload payload;
    if (saveOptions.value("copyAsFile").toBool()) {
        QTemporaryDir directory(QDir::temp().filePath("Still-clipboard-XXXXXX"));
        if (!directory.isValid()) { *error = directory.errorString(); return false; }
        auto options = saveOptions;
        options["dir"] = directory.path();
        const QString path = exportImage(image, options, error);
        if (path.isEmpty()) return false;
        payload.urls = {QUrl::fromLocalFile(path)};
        // shortcut: files survive clipboard/window/app closure; add reference-aware pruning if a cache quota is introduced.
        directory.setAutoRemove(false);
    } else payload.image = image;
    QGuiApplication::clipboard()->setMimeData(restoreClipboard(payload));
    return true;
}

} // namespace still
