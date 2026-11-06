#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

namespace still {

struct ApiProofreadResult {
    QString text, error;
    bool canceled = false;
    qint64 inputTokens = -1, outputTokens = -1, totalTokens = -1;
};

class ApiProofreader : public QObject {
    Q_OBJECT
public:
    explicit ApiProofreader(QObject *parent = nullptr);
    ~ApiProofreader() override;
    void start(const QUrl &endpoint, const QString &model, const QByteArray &key, const QString &text);
    void cancel();
    bool busy() const { return reply_ != nullptr; }
    static QString endpointError(const QUrl &endpoint);
    static QString keyError(const QByteArray &key);
    static QString validationError(const QUrl &endpoint, const QString &model, const QByteArray &key, const QString &text);
signals:
    void finished(const still::ApiProofreadResult &result);
private:
    QNetworkAccessManager network_;
    QNetworkReply *reply_ = nullptr;
    QTimer timeout_;
    QByteArray response_;
    QString failure_;
    bool canceled_ = false;
    void readResponse();
    void finish();
};

} // namespace still

Q_DECLARE_METATYPE(still::ApiProofreadResult)
