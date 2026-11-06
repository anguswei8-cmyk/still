#include "ui/i18n.h"
#include "clipboard_archive.h"
#include "storage/store.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSet>
#include <QtConcurrentRun>
#include <algorithm>

namespace still {
namespace {
constexpr qint64 mib = 1024LL * 1024;
constexpr qint64 maximumIndexBytes = 64 * mib;
bool validId(const QString &id) { static const QRegularExpression pattern("^[a-f0-9]{64}$"); return pattern.match(id).hasMatch(); }
QString payloadPath(const QString &root, const QString &id) { return QDir(root).filePath(id + ".clip"); }
QString normalized(const QString &root) {
    const QFileInfo info(root);
    QString path = QDir::cleanPath(info.exists() ? info.canonicalFilePath() : info.absoluteFilePath());
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}
QString resolveDirectory(const QJsonObject &options) {
    const QString path = options.value("dir").toString().trimmed();
    return path.isEmpty() ? QDir(dataDirectory()).filePath("Clipboard") : QDir(path).absolutePath();
}
bool writeBytes(const QString &path, const QByteArray &bytes, QString &error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = file.errorString(); return false; }
    return true;
}
QByteArray readBytes(const QString &path, qint64 maximum, QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximum) { error = t("无法读取或文件过大：") + QFileInfo(path).fileName(); return {}; }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) { error = file.errorString(); return {}; }
    return bytes;
}
QByteArray digest(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(); }
bool openRoot(const QString &root, std::unique_ptr<QLockFile> &lock, QString &error) {
    QDir directory(root);
    const QString marker = directory.filePath(".still-clipboard");
    if (!QFile::exists(marker) && !QFile::exists(directory.filePath(".pixclone-clipboard"))
        && !directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty()) {
        error = t("剪贴板目录需为空或为已有的 拾影剪贴板专用目录"); return false;
    }
    if (!directory.mkpath(".")) { error = t("无法创建剪贴板目录"); return false; }
    lock = std::make_unique<QLockFile>(directory.filePath("clipboard.lock"));
    if (!lock->tryLock(0)) { error = t("剪贴板目录正被其他实例使用"); lock.reset(); return false; }
    QFile ownership(marker);
    if (!ownership.open(QIODevice::WriteOnly)) { error = ownership.errorString(); lock.reset(); return false; }
    return true;
}
bool readIndex(const QString &root, QVector<ClipEntry> &entries, QString &error, QByteArray *original = nullptr) {
    const QString path = QDir(root).filePath("index.json");
    if (!QFile::exists(path)) return true;
    const auto bytes = readBytes(path, maximumIndexBytes, error);
    if (!error.isEmpty()) return false;
    const auto invalid = [&error] { error = t("剪贴板索引损坏，请保留原目录并选择新的空目录"); return false; };
    const auto object = QJsonDocument::fromJson(bytes).object();
    const auto rows = object.value("entries").toArray();
    // Cleanup is safe only after the entire index has been validated, never a partial or default-empty list.
    if (object.value("version").toInt() != 1 || !object.value("entries").isArray() || rows.size() > 2000) return invalid();
    QVector<ClipEntry> parsed;
    for (const auto &row : rows) {
        const auto item = row.toObject();
        ClipEntry entry;
        entry.id = item.value("id").toString();
        if (!validId(entry.id) || !item.value("kind").isString() || !item.value("title").isString()
            || !item.value("preview").isString() || !item.value("created").isDouble() || !item.value("pinned").isBool()
            || !item.value("formats").isArray() || item.value("formats").toArray().size() > 64 || !item.value("thumbnail").isString()) return invalid();
        for (const auto &format : item.value("formats").toArray())
            if (!format.isString() || format.toString().isEmpty() || format.toString().size() > 256) return invalid();
        if (!QFile::exists(payloadPath(root, entry.id))) continue;
        entry.bytes = QFileInfo(payloadPath(root, entry.id)).size();
        if (entry.bytes > 48 * mib) return invalid();
        entry.kind = item.value("kind").toString().left(20);
        entry.title = item.value("title").toString().left(160);
        entry.preview = item.value("preview").toString().left(4096);
        entry.created = QDateTime::fromMSecsSinceEpoch(item.value("created").toInteger());
        entry.pinned = item.value("pinned").toBool();
        for (const auto &format : item.value("formats").toArray()) entry.formats.append(format.toString());
        const auto encoded = item.value("thumbnail").toString();
        if (!encoded.isEmpty() && encoded.size() < 24000) {
            QByteArray image = QByteArray::fromBase64(encoded.toLatin1());
            QBuffer buffer(&image); buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "PNG");
            const auto size = reader.size();
            if (size.isValid() && size.width() <= 128 && size.height() <= 96) entry.thumbnail = reader.read();
        }
        if (std::none_of(parsed.cbegin(), parsed.cend(), [&](const ClipEntry &old) { return old.id == entry.id; })) parsed.append(entry);
    }
    entries = std::move(parsed);
    if (original) *original = bytes;
    return true;
}
QByteArray indexBytes(const QVector<ClipEntry> &entries) {
    QJsonArray rows;
    for (const auto &entry : entries) {
        QByteArray image;
        if (!entry.thumbnail.isNull()) { QBuffer buffer(&image); buffer.open(QIODevice::WriteOnly); entry.thumbnail.save(&buffer, "PNG"); }
        rows.append(QJsonObject{{"id", entry.id}, {"kind", entry.kind}, {"title", entry.title}, {"preview", entry.preview},
            {"created", double(entry.created.toMSecsSinceEpoch())}, {"pinned", entry.pinned}, {"formats", QJsonArray::fromStringList(entry.formats)},
            {"thumbnail", QString::fromLatin1(image.toBase64())}});
    }
    return QJsonDocument(QJsonObject{{"version", 1}, {"entries", rows}}).toJson(QJsonDocument::Compact);
}
bool saveIndex(const QString &root, const QVector<ClipEntry> &entries, QString &error) {
    const auto bytes = indexBytes(entries);
    if (bytes.size() > maximumIndexBytes) { error = t("剪贴板历史文件过大"); return false; }
    return writeBytes(QDir(root).filePath("index.json"), bytes, error);
}
void sortEntries(QVector<ClipEntry> &entries) {
    std::stable_sort(entries.begin(), entries.end(), [](const ClipEntry &a, const ClipEntry &b) {
        return a.pinned != b.pinned ? a.pinned : a.created > b.created;
    });
}
void trim(QVector<ClipEntry> &entries, const QJsonObject &options) {
    sortEntries(entries);
    const int maximum = qBound(1, options.value("maxItems").toInt(200), 2000);
    const qint64 capacity = qint64(qBound(1, options.value("maxMiB").toInt(256), 10240)) * mib;
    qint64 bytes = 0;
    for (const auto &entry : entries) bytes += entry.bytes;
    for (int i = entries.size() - 1; i >= 0 && (entries.size() > maximum || bytes > capacity); --i)
        if (!entries[i].pinned) { bytes -= entries[i].bytes; entries.removeAt(i); }
}
void cleanup(const QString &root, const QVector<ClipEntry> &entries) {
    QSet<QString> live;
    for (const auto &entry : entries) live.insert(entry.id);
    for (const auto &file : QDir(root).entryInfoList({"*.clip"}, QDir::Files))
        if (validId(file.completeBaseName()) && !live.contains(file.completeBaseName())) QFile::remove(file.absoluteFilePath());
}
} // namespace

struct ClipboardArchive::State {
    QJsonObject options;
    QString root;
    QString blocked;
    QVector<ClipEntry> entries;
    std::unique_ptr<QLockFile> lock;
};
struct ClipboardArchive::Result {
    QString error;
    QString id;
    ClipPayload payload;
    bool loaded = false;
    bool changed = true;
    ReadPurpose purpose = ReadPurpose::Lookup;
    quint64 generation = 0;
};

ClipboardArchive::ClipboardArchive(QJsonObject options, QObject *parent) : QObject(parent), state_(std::make_shared<State>()) {
    qRegisterMetaType<ClipPayload>();
    state_->options = std::move(options);
    state_->root = resolveDirectory(state_->options);
    directory_ = state_->root;
    pool_.setMaxThreadCount(1);
    enqueue([](State &state) {
        Result result;
        const QString requested = state.options.value("dir").toString().trimmed();
        if (!requested.isEmpty() && !QDir::isAbsolutePath(requested)) { state.blocked = result.error = t("剪贴板目录必须使用完整路径"); return result; }
        if (!openRoot(state.root, state.lock, state.blocked) || !readIndex(state.root, state.entries, state.blocked)) result.error = state.blocked;
        else { sortEntries(state.entries); cleanup(state.root, state.entries); }
        return result;
    });
}
ClipboardArchive::~ClipboardArchive() { pool_.waitForDone(); }

void ClipboardArchive::enqueue(std::function<Result(State &)> operation) {
    struct Output { Result result; QVector<ClipEntry> entries; QString root; };
    auto *watcher = new QFutureWatcher<Output>(this);
    connect(watcher, &QFutureWatcher<Output>::finished, this, [this, watcher] {
        const auto output = watcher->result();
        watcher->deleteLater();
        entries_ = output.entries;
        directory_ = output.root;
        if (output.result.loaded) {
            const auto purpose = output.result.purpose;
            // The request may have been canceled after the worker posted this result.
            if (output.result.generation == loadGenerations_[size_t(purpose)])
                emit ready(output.result.id, output.result.payload, output.result.error, purpose);
        }
        else if (!output.result.error.isEmpty()) emit errorOccurred(output.result.error);
        if (output.result.changed) emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&pool_, [state = state_, operation = std::move(operation)] {
        auto result = operation(*state);
        return Output{std::move(result), state->entries, state->root};
    }));
}

void ClipboardArchive::add(ClipPayload payload) {
    const qint64 bytes = payload.bytes();
    if (payload.empty()) return;
    if (queuedItems_ >= 8 || bytes > 128 * mib - queuedBytes_) { emit errorOccurred(t("剪贴板保存繁忙，本次未记录")); return; }
    ++queuedItems_; queuedBytes_ += bytes;
    enqueue([this, bytes, payload = std::move(payload)](State &state) mutable {
        const auto release = qScopeGuard([&] { --queuedItems_; queuedBytes_ -= bytes; });
        Result result;
        if (!state.blocked.isEmpty()) { result.error = state.blocked; return result; }
        ClipEntry entry;
        const auto encoded = encodeClipboard(std::move(payload), &entry, &result.error);
        if (!result.error.isEmpty() || encoded.isEmpty()) return result;
        entry.id = QString::fromLatin1(digest(encoded));
        entry.created = QDateTime::currentDateTime();
        const auto previous = state.entries;
        auto next = previous;
        const auto found = std::find_if(next.begin(), next.end(), [&](const ClipEntry &old) { return old.id == entry.id; });
        const bool duplicate = found != next.end();
        if (duplicate) { entry.pinned = found->pinned; next.erase(found); }
        next.prepend(entry);
        trim(next, state.options);
        // Reject an entry that cannot fit before publishing any eviction of older history.
        if (std::none_of(next.cbegin(), next.cend(), [&](const ClipEntry &old) { return old.id == entry.id; })) {
            result.error = t("剪贴板容量已满或单条内容超过上限，可取消部分固定或增大容量");
            return result;
        }
        if (!duplicate && !writeBytes(payloadPath(state.root, entry.id), encoded, result.error)) return result;
        state.entries = std::move(next);
        QString writeError;
        if (!saveIndex(state.root, state.entries, writeError)) {
            state.entries = previous;
            if (!duplicate) QFile::remove(payloadPath(state.root, entry.id));
            result.error = writeError;
        } else cleanup(state.root, state.entries);
        return result;
    });
}

void ClipboardArchive::load(const QString &id, ReadPurpose purpose) {
    const quint64 generation = ++loadGenerations_[size_t(purpose)];
    enqueue([this, id, purpose, generation](State &state) {
        Result result; result.changed = false;
        if (generation != loadGenerations_[size_t(purpose)]) return result;
        result.purpose = purpose;
        result.generation = generation;
        result.id = id; result.loaded = true;
        if (!validId(id) || std::none_of(state.entries.cbegin(), state.entries.cend(), [&](const ClipEntry &entry) { return entry.id == id; })) { result.error = t("该条历史已删除"); return result; }
        const auto bytes = readBytes(payloadPath(state.root, id), 48 * mib, result.error);
        if (result.error.isEmpty() && QString::fromLatin1(digest(bytes)) != id) result.error = t("剪贴板历史校验失败");
        if (result.error.isEmpty()) result.payload = decodeClipboard(bytes, &result.error);
        if (result.error.isEmpty()) result.error = missingClipboardFiles(result.payload);
        if (generation != loadGenerations_[size_t(purpose)]) { result.payload = {}; result.loaded = false; result.error.clear(); }
        return result;
    });
}

void ClipboardArchive::remove(const QString &id) {
    enqueue([id](State &state) {
        Result result;
        if (!state.blocked.isEmpty()) { result.error = state.blocked; return result; }
        const auto previous = state.entries;
        state.entries.erase(std::remove_if(state.entries.begin(), state.entries.end(), [&](const ClipEntry &entry) { return entry.id == id; }), state.entries.end());
        if (!saveIndex(state.root, state.entries, result.error)) state.entries = previous;
        else cleanup(state.root, state.entries);
        return result;
    });
}
void ClipboardArchive::pin(const QString &id, bool pinned) {
    enqueue([id, pinned](State &state) {
        Result result;
        if (!state.blocked.isEmpty()) { result.error = state.blocked; return result; }
        const auto previous = state.entries;
        for (auto &entry : state.entries) if (entry.id == id) entry.pinned = pinned;
        sortEntries(state.entries);
        if (!saveIndex(state.root, state.entries, result.error)) state.entries = previous;
        return result;
    });
}
void ClipboardArchive::clear(bool includePinned) {
    enqueue([includePinned](State &state) {
        Result result;
        if (!state.blocked.isEmpty()) { result.error = state.blocked; return result; }
        const auto previous = state.entries;
        state.entries.erase(std::remove_if(state.entries.begin(), state.entries.end(), [=](const ClipEntry &entry) { return includePinned || !entry.pinned; }), state.entries.end());
        if (!saveIndex(state.root, state.entries, result.error)) state.entries = previous;
        else cleanup(state.root, state.entries);
        return result;
    });
}

bool ClipboardArchive::reconfigureBlocking(QJsonObject options, std::function<bool(QString &)> commit, QString &error) {
    struct Request { QSemaphore done; bool success = false; QString error; };
    const auto request = std::make_shared<Request>();
    // Semaphore waiting cannot steal this task ahead of previously accepted writes.
    auto future = QtConcurrent::run(&pool_, [state = state_, options, commit = std::move(commit), request] {
        const auto finish = qScopeGuard([&] { request->done.release(); });
        QString &error = request->error;
        const QString requested = options.value("dir").toString().trimmed();
        if (!requested.isEmpty() && !QDir::isAbsolutePath(requested)) { error = t("剪贴板目录必须使用完整路径"); return; }
        const QString destination = resolveDirectory(options);
        const bool ownsSource = bool(state->lock) && state->blocked.isEmpty();
        const QString from = normalized(state->root), to = normalized(destination);
        if (from != to && (to.startsWith(from + '/') || from.startsWith(to + '/'))) { error = t("新旧剪贴板目录不能互相包含"); return; }
        QVector<ClipEntry> next = state->entries;
        std::unique_ptr<QLockFile> targetLock;
        const QString indexPath = QDir(destination).filePath("index.json");
        QByteArray previousIndex;
        QStringList copied;
        const auto discardCopies = qScopeGuard([&] {
            if (!request->success) for (const auto &path : copied) QFile::remove(path);
        });
        if (from != to) {
            if (!openRoot(destination, targetLock, error)) return;
            QVector<ClipEntry> existing;
            if (!readIndex(destination, existing, error, &previousIndex)) return;
            for (const auto &entry : existing) {
                const auto old = std::find_if(next.begin(), next.end(), [&](const ClipEntry &candidate) { return candidate.id == entry.id; });
                if (old == next.end()) next.append(entry);
                else { old->pinned = old->pinned || entry.pinned; if (entry.created > old->created) old->created = entry.created; }
            }
            if (next.size() > 2000) { error = t("合并后的历史超过 2000 条，请先清理一个目录"); return; }
            for (const auto &entry : state->entries) {
                const auto bytes = readBytes(payloadPath(state->root, entry.id), 48 * mib, error);
                if (!error.isEmpty() || QString::fromLatin1(digest(bytes)) != entry.id) { if (error.isEmpty()) error = t("原剪贴板历史校验失败"); return; }
                const QString path = payloadPath(destination, entry.id);
                if (!QFile::exists(path)) {
                    if (!writeBytes(path, bytes, error)) return;
                    copied.append(path);
                }
                const auto copiedBytes = readBytes(path, 48 * mib, error);
                if (!error.isEmpty() || digest(copiedBytes) != digest(bytes)) { error = t("剪贴板迁移校验失败"); return; }
            }
            // Either directory must remain readable if the process exits at the settings checkpoint.
            sortEntries(next);
            if (!saveIndex(destination, next, error)) return;
        } else if (!state->blocked.isEmpty()) { error = state->blocked; return; }
        if (!commit || !commit(error)) {
            if (from != to) {
                QString rollbackError;
                const bool restored = !previousIndex.isEmpty() ? writeBytes(indexPath, previousIndex, rollbackError) : QFile::remove(indexPath);
                if (!restored) {
                    // Keep copies that the durable merged index may still reference.
                    copied.clear();
                    error += QStringLiteral("\n") + (rollbackError.isEmpty() ? indexPath : rollbackError);
                }
            }
            return;
        }
        const QString source = state->root;
        const auto originals = state->entries;
        state->root = destination;
        state->options = options;
        state->blocked.clear();
        state->entries = next;
        std::unique_ptr<QLockFile> oldLock;
        if (targetLock) { oldLock = std::move(state->lock); state->lock = std::move(targetLock); }
        auto retained = state->entries;
        trim(retained, state->options);
        QString warning;
        const bool needsIndex = from == to || retained.size() != state->entries.size();
        const bool quotaSaved = !needsIndex || saveIndex(destination, retained, warning);
        if (quotaSaved) {
            state->entries = std::move(retained);
            cleanup(destination, state->entries);
        } else error = t("设置已保存，剪贴板配额暂未应用；已有条目仍可读取");
        if (from != to && ownsSource && quotaSaved) {
            QVector<ClipEntry> remaining;
            for (const auto &entry : originals)
                if (!QFile::remove(payloadPath(source, entry.id))) remaining.append(entry);
            if (!saveIndex(source, remaining, warning) || !remaining.isEmpty())
                error = t("新剪贴板目录已启用，部分旧副本未能清理");
        }
        request->success = true;
    });
    request->done.acquire();
    error = request->error;
    QMetaObject::invokeMethod(this, [this] { enqueue([](State &) { return Result{}; }); }, Qt::QueuedConnection);
    return request->success;
}

} // namespace still
