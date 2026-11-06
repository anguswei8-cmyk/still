#include "ui/i18n.h"
#include "scroll_session.h"
#include "imaging/image.h"

#include <QFutureWatcher>
#include <QtConcurrentRun>

namespace still {

ScrollSession::ScrollSession(Capture capture, QObject *parent) : QObject(parent), capture_(std::move(capture)) {
    connect(&timer_, &QTimer::timeout, this, &ScrollSession::sample);
}

bool ScrollSession::start(const QRect &region, const QImage &first, ScrollOptions options) {
    if (active() || !capture_ || region.isEmpty() || first.isNull() || first.height() > options.maximumHeight || first.sizeInBytes() > options.maximumBytes) return false;
    region_ = region;
    image_ = first;
    options_ = options;
    options_.maximumFrames = qBound(2, options_.maximumFrames, 300);
    frames_ = 1;
    ++generation_;
    // A prior stopped session may still have one native-sized join in flight; never queue another.
    timer_.start(qBound(200, options.intervalMs, 5000));
    return true;
}

void ScrollSession::stop() { finish({}); }

void ScrollSession::finish(const QString &message) {
    if (!active()) return;
    timer_.stop();
    ++generation_;
    const QImage result = std::move(image_);
    const QRect region = region_;
    region_ = {};
    frames_ = 0;
    emit finished(result, region, message);
}

void ScrollSession::sample() {
    if (!active() || inFlight_) return;
    const quint64 generation = generation_;
    QImage frame = capture_(region_);
    if (!active() || generation != generation_) return;
    if (frame.isNull()) { finish(t("滚动帧捕获失败，已保留最后成功的结果")); return; }
    if (frame.width() != image_.width()) frame = frame.scaledToWidth(image_.width(), Qt::SmoothTransformation);
    if (frame.isNull()) { finish(t("滚动帧捕获失败，已保留最后成功的结果")); return; }
    const QImage previous = image_;
    const auto options = options_;
    auto *watcher = new QFutureWatcher<JoinResult>(this);
    inFlight_ = watcher;
    connect(watcher, &QFutureWatcher<JoinResult>::finished, this, [this, watcher, generation] {
        const auto joined = watcher->result();
        inFlight_ = nullptr;
        watcher->deleteLater();
        if (!active() || generation != generation_) return;
        // Smooth scrolling and transient UI changes can produce an unusable sample.
        if (joined.unmatched) { emit alignmentPending(); return; }
        if (joined.image.isNull()) { finish(t("长截图达到大小上限或无法继续拼接，已保留最后成功的结果")); return; }
        if (joined.image.height() == image_.height()) return;
        image_ = joined.image;
        ++frames_;
        emit progress(image_.size(), frames_);
        if (frames_ >= options_.maximumFrames) finish({});
    });
    watcher->setFuture(QtConcurrent::run([previous, frame, options] {
        JoinResult result;
        result.image = appendScrollFrame(previous, frame, options.maximumHeight, options.maximumBytes, &result.unmatched);
        return result;
    }));
}

} // namespace still
