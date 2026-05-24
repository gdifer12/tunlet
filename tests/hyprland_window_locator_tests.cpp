#include "app/hyprland_window_locator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPair>
#include <QTemporaryDir>
#include <QTest>

#include <future>
#include <stdexcept>

namespace {

QApplication *testApplication() {
    static int argc = 1;
    static char appName[] = "tunlet_hyprland_locator_tests";
    static char *argv[] = {appName, nullptr};
    qputenv("QT_QPA_PLATFORM", "offscreen");
    static QApplication application(argc, argv);
    return &application;
}

struct FakeHyprlandServer {
    QLocalServer server;
    QList<QByteArray> responses;
    QList<QByteArray> commands;

    bool listen(const QString &socketPath) {
        QFile::remove(socketPath);
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this]() {
            while (server.hasPendingConnections()) {
                QLocalSocket *socket = server.nextPendingConnection();
                QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket]() {
                    const QByteArray command = socket->readAll();
                    commands.push_back(command);
                    const QByteArray response = responses.isEmpty() ? QByteArray("ok") : responses.takeFirst();
                    socket->write(response);
                    socket->flush();
                    socket->waitForBytesWritten(200);
                    socket->disconnectFromServer();
                });
                QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        return server.listen(socketPath);
    }
};

template <typename T>
T waitForFuture(std::future<T> &future) {
    for (int index = 0; index < 200; ++index) {
        if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            return future.get();
        }
        QCoreApplication::processEvents();
        QTest::qWait(5);
    }
    throw std::runtime_error("future did not complete in time");
}

}  // namespace

TEST_CASE("HyprlandWindowLocator parses current-workspace clients for the current process", "[hyprland]") {
    testApplication();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString signature = "tunlet-test-signature";
    const QString socketDir = dir.path() + "/hypr/" + signature;
    REQUIRE(QDir().mkpath(socketDir));
    const QString socketPath = socketDir + "/.socket.sock";

    qputenv("XDG_RUNTIME_DIR", dir.path().toUtf8());
    qputenv("HYPRLAND_INSTANCE_SIGNATURE", signature.toUtf8());

    FakeHyprlandServer fakeServer;
    fakeServer.responses = {
        QByteArray(R"({"id":7})"),
        QByteArray(
            QString(R"([
                {"pid":%1,"address":"0xabc","title":"tunlet [#1]","workspace":{"id":7}},
                {"pid":%1,"address":"0xdef","title":"tunlet [#2]","workspace":{"id":8}},
                {"pid":999999,"address":"0xghi","title":"other","workspace":{"id":7}}
            ])")
                .arg(QCoreApplication::applicationPid())
                .toUtf8()),
    };
    REQUIRE(fakeServer.listen(socketPath));

    tunlet::app::HyprlandWindowLocator locator;
    auto future = std::async(std::launch::async, [&locator]() { return locator.snapshotForCurrentProcess(); });
    const auto snapshot = waitForFuture(future);

    REQUIRE(snapshot.ok);
    REQUIRE(snapshot.activeWorkspaceId == 7);
    REQUIRE(snapshot.currentWorkspaceClients.size() == 1);
    REQUIRE(snapshot.currentWorkspaceClients.at(0).title == "tunlet [#1]");
    REQUIRE(snapshot.currentWorkspaceClients.at(0).address == "0xabc");
    REQUIRE(snapshot.currentWorkspaceClients.at(0).workspaceId == 7);
    REQUIRE(fakeServer.commands.size() == 2);
    REQUIRE(fakeServer.commands.at(0) == QByteArray("j/activeworkspace"));
    REQUIRE(fakeServer.commands.at(1) == QByteArray("j/clients"));
}

TEST_CASE("HyprlandWindowLocator sends focus dispatch by window address", "[hyprland]") {
    testApplication();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString signature = "tunlet-test-focus";
    const QString socketDir = dir.path() + "/hypr/" + signature;
    REQUIRE(QDir().mkpath(socketDir));
    const QString socketPath = socketDir + "/.socket.sock";

    qputenv("XDG_RUNTIME_DIR", dir.path().toUtf8());
    qputenv("HYPRLAND_INSTANCE_SIGNATURE", signature.toUtf8());

    FakeHyprlandServer fakeServer;
    fakeServer.responses = {QByteArray("ok")};
    REQUIRE(fakeServer.listen(socketPath));

    tunlet::app::HyprlandWindowLocator locator;
    auto future = std::async(std::launch::async, [&locator]() {
        QString error;
        const bool focused = locator.focusWindowByAddress("0xfeed", &error);
        return qMakePair(focused, error);
    });
    const auto result = waitForFuture(future);

    REQUIRE(result.first);
    REQUIRE(result.second.isEmpty());
    REQUIRE(fakeServer.commands.size() == 1);
    REQUIRE(fakeServer.commands.at(0) == QByteArray("dispatch focuswindow address:0xfeed"));
}
