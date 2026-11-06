#pragma once

#include <QDateTime>
#include <QFuture>
#include <QFutureWatcher>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QReadWriteLock>
#include <QThreadPool>
#include <atomic>
#include <functional>

namespace still {

struct HistoryEntry {
    QString path;
    QString name;
    QDateTime modified;
};

struct HistoryListing {
    QList<HistoryEntry> entries;
    qint64 bytes = 0;
};

class HistoryStore : public QObject {
    Q_OBJECT
public:
    explicit HistoryStore(const QJsonObject &options, QObject *parent = nullptr);
    ~HistoryStore() override;
    void remember(const QImage &image);
    void remove(const QString &path);
    void clear();
    QString directory() const { return directory_; }
    QFuture<HistoryListing> list();
    QFuture<QList<QImage>> thumbnails(QStringList paths, QSize maximumSize);
    QFuture<QImage> readImage(QString path);
    void reconfigure(const QJsonObject &options, std::function<bool(QString &)> commit);
    // Complete the active configuration synchronously, in write-queue order.
    void waitForConfiguration();
    bool isConfiguring() const { return configuration_ != nullptr; }
signals:
    void changed();
    void errorOccurred(const QString &message);
    void reconfigured(bool success, const QString &message);
private:
    // Only the single worker thread accesses these options and duplicate-detection fields.
    QJsonObject options_;
    QByteArray lastHash_;
    QString lastPath_;
    QString directory_;
    QThreadPool pool_;
    QThreadPool readPool_;
    std::atomic<qint64> queuedBytes_{0};
    std::atomic_int queuedImages_{0};
    QFutureWatcher<QPair<bool, QString>> *configuration_ = nullptr;
    QString configuringDirectory_;
    QReadWriteLock fileAccess_;
    QImage read(const QString &path, const QSize &maximumSize);
    void erase(const QString &path, bool all);
    void completeConfiguration();
};

} // namespace still
