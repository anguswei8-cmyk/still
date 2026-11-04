#pragma once

#include "clipboard_data.h"
#include <QObject>
#include <QJsonObject>
#include <QThreadPool>
#include <array>
#include <atomic>
#include <functional>
#include <memory>

namespace pixclone {

class ClipboardArchive : public QObject {
    Q_OBJECT
public:
    enum class ReadPurpose { Lookup, Preview, Action };
    Q_ENUM(ReadPurpose)

    explicit ClipboardArchive(QJsonObject options, QObject *parent = nullptr);
    ~ClipboardArchive() override;
    QVector<ClipEntry> entries() const { return entries_; }
    void add(ClipPayload payload);
    void load(const QString &id, ReadPurpose purpose = ReadPurpose::Lookup);
    void cancelLoads(ReadPurpose purpose) { ++loadGenerations_[size_t(purpose)]; }
    void remove(const QString &id);
    void pin(const QString &id, bool pinned);
    void clear(bool includePinned = false);
    QString directory() const { return directory_; }
    // Used only by SettingsApply's worker checkpoint; no GUI event loop is needed to finish.
    bool reconfigureBlocking(QJsonObject options, std::function<bool(QString &)> commit, QString &error);
signals:
    void changed();
    void errorOccurred(const QString &message);
    void ready(const QString &id, const pixclone::ClipPayload &payload, const QString &error, ReadPurpose purpose);
private:
    struct State;
    struct Result;
    std::shared_ptr<State> state_;
    QThreadPool pool_;
    QVector<ClipEntry> entries_;
    QString directory_;
    std::atomic<qint64> queuedBytes_{0};
    std::atomic_int queuedItems_{0};
    std::array<std::atomic<quint64>, 3> loadGenerations_{};
    void enqueue(std::function<Result(State &)> operation);
};

} // namespace pixclone

Q_DECLARE_METATYPE(pixclone::ClipPayload)
