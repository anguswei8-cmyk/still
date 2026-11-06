#pragma once

#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <memory>

class ApiTestServer : public QObject {
public:
    QTcpServer server;
    QList<QByteArray> requests;
    QList<QPointer<QTcpSocket>> peers;
    QByteArray body = R"JSON({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"corrected"}}],"usage":{"prompt_tokens":12,"completion_tokens":8,"total_tokens":20}})JSON";
    QByteArray extraHeaders, requestHeaders, requestBody;
    int status = 200, declaredLength = -1;
    bool hold = false;

    ApiTestServer() {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection(); peers.append(socket);
                auto data = std::make_shared<QByteArray>();
                connect(socket, &QIODevice::readyRead, this, [this, socket, data] {
                    *data += socket->readAll();
                    const int end = data->indexOf("\r\n\r\n"); if (end < 0) return;
                    int length = -1;
                    for (auto line : data->left(end).split('\n')) {
                        line = line.trimmed();
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                    }
                    if (length < 0 || data->size() < end + 4 + length) return;
                    requestHeaders = data->left(end); requestBody = data->mid(end + 4, length);
                    requests.append(*data); disconnect(socket, &QIODevice::readyRead, this, nullptr);
                    if (!hold) {
                        socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Type: application/json\r\nContent-Length: "
                            + QByteArray::number(declaredLength < 0 ? body.size() : declaredLength) + "\r\n" + extraHeaders + "Connection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    }
                });
            }
        });
    }
    QUrl endpoint() const { return QUrl(QString("http://127.0.0.1:%1/v1/chat/completions").arg(server.serverPort())); }
};
