#include "ui/i18n.h"
#include "history.h"
#include "store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QPromise>
#include <QReadWriteLock>
#include <QSaveFile>
#include <QScopeGuard>
#include <QUuid>
#include <QtConcurrentRun>

namespace still {
namespace {

QString normalizedPath(const QString &path) {
    const QFileInfo info(path);
    QString result = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return QDir::cleanPath(result);
}

QByteArray fileHash(const QString &path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
    return hash.result();
}

using MigrationFiles = QList<QPair<QString, QString>>;

bool copyHistory(const QString &source, const QString &destination, MigrationFiles &copied, QString &error) {
    const QString from = normalizedPath(source), to = normalizedPath(destination);
    if (from == to) return true;
    if (to.startsWith(from + '/') || from.startsWith(to + '/')) {
        error = t("新旧历史目录不能互相包含，请选择独立文件夹");
        return false;
    }
    QDir target(destination);
    const QString marker = target.filePath(".still-history");
    if (!QFileInfo::exists(marker) && !QFileInfo::exists(target.filePath(".pixclone-history"))
        && !target.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty()) {
        error = t("请选择空文件夹或已有的 拾影历史专用文件夹，避免影响其他文件");
        return false;
    }
    if (!target.mkpath(".")) { error = t("无法创建历史目录"); return false; }
    QFile ownership(marker);
    if (!ownership.open(QIODevice::WriteOnly)) { error = ownership.errorString(); return false; }
    ownership.close();
    QStringList created;
    const auto rollback = qScopeGuard([&] {
        if (!error.isEmpty()) for (const auto &path : created) QFile::remove(path);
    });
    for (const auto &path : historyFiles(source)) {
        const QByteArray digest = fileHash(path);
        if (digest.isEmpty()) { error = t("无法读取旧历史：") + path; return false; }
        QString next = target.filePath(QFileInfo(path).fileName());
        if (QFileInfo(next).isSymLink() || (QFileInfo::exists(next) && fileHash(next) != digest))
            next = target.filePath(QUuid::createUuid().toString(QUuid::Id128) + ".png");
        if (!QFileInfo::exists(next)) {
            QFile input(path);
            QSaveFile output(next);
            if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) { error = t("无法复制历史图片"); return false; }
            while (!input.atEnd()) {
                const QByteArray block = input.read(1024 * 1024);
                if (block.isEmpty() || output.write(block) != block.size()) { error = t("历史复制失败，请检查剩余空间"); return false; }
            }
            if (!output.commit()) { error = output.errorString(); return false; }
            created.append(next);
            QFile metadata(next);
            if (metadata.open(QIODevice::ReadWrite)) metadata.setFileTime(QFileInfo(path).lastModified(), QFileDevice::FileModificationTime);
        }
        if (fileHash(next) != digest) { error = t("历史图片复制校验失败"); return false; }
        copied.append({path, next});
    }
    return true;
}

int maximumItems(const QJsonObject &options) { return qBound(1, options.value("maxItems").toInt(200), 5000); }
qint64 maximumBytes(const QJsonObject &options) { return qint64(qBound(1, options.value("maxMiB").toInt(1024), 102400)) * 1024 * 1024; }

} // namespace

HistoryStore::HistoryStore(const QJsonObject &options, QObject *parent)
    : QObject(parent), options_(options), directory_(historyDirectory(options)) {
    if (options_.value("dir").toString().trimmed().isEmpty()) options_["dir"] = directory_;
    pool_.setMaxThreadCount(1);
    readPool_.setMaxThreadCount(1);
}

HistoryStore::~HistoryStore() { pool_.waitForDone(); readPool_.waitForDone(); }

QFuture<HistoryListing> HistoryStore::list() {
    return QtConcurrent::run(&readPool_, [this] {
        const QReadLocker lock(&fileAccess_);
        const QDir directory(historyDirectory(options_));
        HistoryListing listing;
        for (const auto &file : directory.entryInfoList({"*.png"}, QDir::Files, QDir::Time)) {
            listing.entries.append({file.absoluteFilePath(), file.fileName(), file.lastModified()});
            listing.bytes += file.size();
        }
        return listing;
    });
}

QFuture<QList<QImage>> HistoryStore::thumbnails(QStringList paths, QSize maximumSize) {
    return QtConcurrent::run(&readPool_, [this, paths = std::move(paths), maximumSize](QPromise<QList<QImage>> &promise) {
        QList<QImage> images;
        for (const auto &path : paths) {
            if (promise.isCanceled()) return;
            images.append(read(path, maximumSize));
        }
        promise.addResult(std::move(images));
    });
}

QFuture<QImage> HistoryStore::readImage(QString path) {
    return QtConcurrent::run(&readPool_, [this, path = std::move(path)] { return read(path, {}); });
}

QImage HistoryStore::read(const QString &path, const QSize &maximumSize) {
    const QReadLocker lock(&fileAccess_);
    const QFileInfo file(path);
    if (file.absolutePath() != QDir(historyDirectory(options_)).absolutePath() || file.suffix().compare("png", Qt::CaseInsensitive) != 0) return {};
    QImageReader reader(path);
    const QSize size = reader.size();
    if (size.isValid() && maximumSize.isValid()) reader.setScaledSize(size.scaled(maximumSize, Qt::KeepAspectRatio));
    return reader.read();
}

void HistoryStore::remember(const QImage &image) {
    if (image.isNull()) return;
    const qint64 bytes = image.sizeInBytes();
    // Bound retained screenshots while a slow disk or directory migration blocks the worker.
    if (queuedImages_ >= 8 || bytes > 256LL * 1024 * 1024 - queuedBytes_) {
        emit errorOccurred(t("历史保存繁忙，本次未加入历史；图片仍可手动保存"));
        return;
    }
    ++queuedImages_;
    queuedBytes_ += bytes;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const QString error = watcher->result();
        watcher->deleteLater();
        if (!error.isEmpty()) emit errorOccurred(error);
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&pool_, [this, image, bytes] {
        const auto release = qScopeGuard([&] { queuedBytes_ -= bytes; --queuedImages_; });
        if (!options_.value("enabled").toBool(true)) return QString();
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QByteArray::number(image.width()) + ':' + QByteArray::number(image.height()) + ':' + QByteArray::number(image.format()) + ':');
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()));
        const QByteArray digest = hash.result();
        const QWriteLocker lock(&fileAccess_);
        if (digest == lastHash_ && QFileInfo::exists(lastPath_)) return QString();
        QString error;
        const QString path = addHistory(image, maximumItems(options_), &error, historyDirectory(options_), maximumBytes(options_));
        if (!path.isEmpty()) { lastHash_ = digest; lastPath_ = path; }
        return error;
    }));
}

void HistoryStore::remove(const QString &path) { erase(path, false); }
void HistoryStore::clear() { erase({}, true); }

void HistoryStore::erase(const QString &path, bool all) {
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const QString error = watcher->result();
        watcher->deleteLater();
        if (!error.isEmpty()) emit errorOccurred(error);
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&pool_, [this, path, all] {
        const QWriteLocker lock(&fileAccess_);
        // Resolve the live directory inside the same queue as writes and migration.
        const auto files = historyFiles(historyDirectory(options_));
        if (!all && !files.contains(QFileInfo(path).absoluteFilePath()))
            return t("该图片已不在当前历史目录中，请刷新后重试");
        QStringList failures;
        for (const auto &file : (all ? files : QStringList{QFileInfo(path).absoluteFilePath()}))
            if (!QFile::remove(file)) failures.append(QFileInfo(file).fileName());
        lastHash_.clear();
        lastPath_.clear();
        return failures.isEmpty() ? QString() : t("部分历史无法删除：") + failures.join(QStringLiteral("、"));
    }));
}

void HistoryStore::reconfigure(const QJsonObject &options, std::function<bool(QString &)> commit) {
    if (isConfiguring()) return;
    if (!commit) { emit reconfigured(false, t("缺少设置提交步骤")); return; }
    auto *watcher = new QFutureWatcher<QPair<bool, QString>>(this);
    configuration_ = watcher;
    configuringDirectory_ = historyDirectory(options);
    connect(watcher, &QFutureWatcher<QPair<bool, QString>>::finished, this, &HistoryStore::completeConfiguration);
    watcher->setFuture(QtConcurrent::run(&pool_, [this, next = options, commit = std::move(commit)]() -> QPair<bool, QString> {
        const QString requested = next.value("dir").toString().trimmed();
        if (!requested.isEmpty() && !QDir::isAbsolutePath(requested)) return {false, t("历史目录必须使用完整路径")};
        const QString source = historyDirectory(options_), destination = historyDirectory(next);
        auto effective = next;
        if (requested.isEmpty()) effective["dir"] = destination;
        QString error;
        if (effective == options_) { const bool saved = commit(error); return {saved, error}; }
        const QWriteLocker lock(&fileAccess_);
        MigrationFiles copied;
        if (!copyHistory(source, destination, copied, error)) return {false, error};
        // Originals are only removed after the new setting is atomically committed.
        if (!commit(error)) return {false, error};
        options_ = effective;
        QString cleanupWarning;
        for (const auto &pair : copied) {
            const auto digest = fileHash(pair.first);
            if (digest.isEmpty() || digest != fileHash(pair.second) || !QFile::remove(pair.first))
                cleanupWarning = t("新目录已启用，部分旧副本未删除，可核对后手动清理");
        }
        if (!copied.isEmpty()) {
            QFile marker(QDir(source).filePath(".still-history"));
            if (marker.open(QIODevice::WriteOnly)) marker.close();
        }
        lastHash_.clear();
        lastPath_.clear();
        QString quotaWarning;
        pruneHistory(destination, maximumItems(options_), maximumBytes(options_), &quotaWarning);
        QStringList warnings;
        for (const auto &warning : {error, cleanupWarning, quotaWarning}) if (!warning.isEmpty()) warnings.append(warning);
        return {true, warnings.join('\n')};
    }));
}

void HistoryStore::waitForConfiguration() {
    // Wait in queue order without dispatching unrelated UI events during shutdown.
    pool_.waitForDone();
    completeConfiguration();
}

void HistoryStore::completeConfiguration() {
    const auto result = configuration_->result();
    configuration_->disconnect(this);
    configuration_->deleteLater();
    configuration_ = nullptr;
    if (result.first) directory_ = configuringDirectory_;
    emit reconfigured(result.first, result.second);
    if (result.first) emit changed();
}

} // namespace still
