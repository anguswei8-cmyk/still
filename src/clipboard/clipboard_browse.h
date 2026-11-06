#pragma once

#include "clipboard_archive.h"
#include <QHash>
#include <QTimer>
#include <QVariantList>

namespace still {
class ClipboardBrowse : public QObject {
    Q_OBJECT
public:
    explicit ClipboardBrowse(ClipboardArchive &archive, QObject *parent = nullptr);
    ~ClipboardBrowse() override;
    QVariantList clips() const { return clips_; }
    const QHash<QString, QImage> &thumbnails() const { return thumbnails_; }
    QString search() const { return search_; }
    void setSearch(const QString &search);
    QString filter() const { return filter_; }
    void setFilter(const QString &filter);
    int selectedIndex() const { return selectedIndex_; }
    bool setSelectedIndex(int index);
    QString selectedId() const { return selectedId_; }
    QVariantMap selected() const;
    bool previewVisible() const { return previewVisible_; }
    void setPreviewVisible(bool visible);
    QString previewText() const { return previewText_; }
    QString previewUrl() const { return previewUrl_; }
    const QImage &previewImage() const { return previewImage_; }
    void setActive(bool active);
    void setPreviewFits(bool fits);
    void refreshClips();
signals:
    void clipsChanged();
    void searchChanged();
    void filterChanged();
    void selectionChanged();
    void previewChanged();
    void message(const QString &message);
private:
    ClipboardArchive &archive_;
    QString search_, filter_ = QStringLiteral("all"), selectedId_;
    QVariantList clips_;
    QHash<QString, QImage> thumbnails_;
    int selectedIndex_ = -1;
    bool active_ = false;
    bool previewFits_ = false;
    bool previewVisible_ = false;
    QString previewText_, previewUrl_;
    QImage previewImage_;
    quint64 previewRevision_ = 0;
    QTimer previewTimer_;
    bool canPreview() const;
    void updatePreview();
};
} // namespace still
