#include "ui/i18n.h"
#include "application.h"
#include "storage/store.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QTimer>
#include <QQuickWidget>

int main(int argc, char **argv) {
    using still::t;
    QApplication app(argc, argv);
    QApplication::setApplicationName("Still");
    QApplication::setApplicationVersion("0.2.0");
    QApplication::setOrganizationName("Still");
    QApplication::setStyle("Fusion");
    still::InterfacePreferences::instance().apply(still::readJson(still::settingsPath()).value("appearance").toObject());
    QApplication::setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.setApplicationDescription(t("离线 Qt 截图、编辑、贴图与文字选择"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"capture", t("启动后截图")});
    parser.addOption({"smoke-test", t("启动界面并在一秒后退出，用于部署检查")});
    parser.addPositionalArgument("image", t("打开图片进行编辑"), "[image]");
    parser.process(app);

    QDir().mkpath(still::dataDirectory());
    const QString identity = QString::fromLatin1(QCryptographicHash::hash(still::dataDirectory().toUtf8(), QCryptographicHash::Sha256).toHex().left(20));
    const QString name = "StillQt-" + identity;
    QLockFile lock(QDir(still::dataDirectory()).filePath("qt-instance.lock"));
    if (!lock.tryLock(100)) {
        QLocalSocket socket;
        socket.connectToServer(name);
        if (socket.waitForConnected(1000)) { socket.write("show"); socket.waitForBytesWritten(1000); return 0; }
        qCritical("Still is already running or its instance lock is unavailable.");
        return 1;
    }
    QLocalServer::removeServer(name);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(name)) { qCritical("Unable to create the single-instance server."); return 1; }
    still::Application controller(nullptr, !parser.isSet("smoke-test"));
    QObject::connect(&server, &QLocalServer::newConnection, &controller, [&] {
        while (auto *socket = server.nextPendingConnection()) { controller.openSettings(); socket->disconnectFromServer(); socket->deleteLater(); }
    });
    if (!parser.positionalArguments().isEmpty()) controller.openImage(parser.positionalArguments().first());
    if (parser.isSet("capture")) QTimer::singleShot(250, &controller, [&] { controller.capture(); });
    if (parser.isSet("smoke-test")) QTimer::singleShot(1000, &app, [&app] {
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *window = qobject_cast<still::MainWindow *>(widget)) {
            app.exit(window->view()->status() == QQuickWidget::Ready ? 0 : 1); return;
        }
        app.exit(1);
    });
    return app.exec();
}
