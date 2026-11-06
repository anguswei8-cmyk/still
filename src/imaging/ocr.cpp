#include "ocr.h"
#include "platform/platform.h"
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QPointer>
#include <QThreadPool>
#include <QtConcurrentRun>

namespace still {

namespace {
struct OcrQueue {
    QThreadPool pool;
    QList<OcrJob *> pending;
    int running = 0;
    OcrQueue() { pool.setMaxThreadCount(2); }
};

OcrQueue &queue() { static OcrQueue value; return value; }
} // namespace

OcrJob::OcrJob(const QImage &image, const QString &language, QObject *parent)
    : QObject(parent), image_(image), language_(language) {
    queue().pending.append(this);
    startPending();
}

OcrJob::~OcrJob() { queue().pending.removeOne(this); }

void OcrJob::startPending() {
    auto &state = queue();
    // Pending images belong to their jobs, so closing/replacing a window releases them immediately.
    // Only the two active native calls own independent images until the OS recognizer returns.
    while (state.running < 2 && !state.pending.isEmpty()) {
        auto *job = state.pending.takeFirst();
        ++state.running;
        auto *watcher = new QFutureWatcher<OcrResult>(QCoreApplication::instance());
        const QPointer<OcrJob> guard(job);
        connect(watcher, &QFutureWatcher<OcrResult>::finished, watcher, [watcher, guard] {
            --queue().running;
            if (guard) emit guard->finished(watcher->result());
            if (guard) guard->deleteLater();
            watcher->deleteLater();
            startPending();
        });
        const QImage image = std::move(job->image_);
        const QString language = job->language_;
        watcher->setFuture(QtConcurrent::run(&state.pool, [image, language] { return recognizeNative(image, language); }));
    }
}

} // namespace still
