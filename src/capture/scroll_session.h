#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <QTimer>
#include <functional>

template<class T> class QFutureWatcher;

namespace still {

struct ScrollOptions {
    int intervalMs = 420;
    int maximumHeight = 24000;
    int maximumFrames = 80;
    qint64 maximumBytes = 256LL * 1024 * 1024;
};

class ScrollSession : public QObject {
    Q_OBJECT
public:
    using Capture = std::function<QImage(const QRect &)>;
    explicit ScrollSession(Capture capture, QObject *parent = nullptr);
    bool start(const QRect &region, const QImage &first, ScrollOptions options = {});
    void stop();
    bool active() const { return timer_.isActive(); }
    const QImage &image() const { return image_; }
signals:
    void progress(const QSize &size, int frames);
    void alignmentPending();
    void finished(const QImage &image, const QRect &region, const QString &message);
private:
    Capture capture_;
    QTimer timer_;
    QImage image_;
    QRect region_;
    ScrollOptions options_;
    int frames_ = 0;
    quint64 generation_ = 0;
    struct JoinResult { QImage image; bool unmatched = false; };
    QFutureWatcher<JoinResult> *inFlight_ = nullptr;
    void sample();
    void finish(const QString &message);
};

} // namespace still
