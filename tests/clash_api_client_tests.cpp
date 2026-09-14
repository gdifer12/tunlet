#include "clash/clash_api_client.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace {

QCoreApplication *testApplication() {
    if (QCoreApplication::instance()) {
        return QCoreApplication::instance();
    }
    static int argc = 1;
    static char appName[] = "tunlet_clash_api_tests";
    static char *argv[] = {appName, nullptr};
    return new QCoreApplication(argc, argv);
}

bool requestIsComplete(const QByteArray &request) {
    const qsizetype headerEnd = request.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return false;
    }

    qsizetype contentLength = 0;
    for (const auto &line : request.left(headerEnd).split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith("content-length:")) {
            contentLength = trimmed.mid(QByteArray("content-length:").size()).trimmed().toLongLong();
            break;
        }
    }
    return request.size() >= headerEnd + 4 + contentLength;
}

void writeHttpResponse(QTcpSocket *socket, int status, const QByteArray &body = {}) {
    const QByteArray statusText = status == 204 ? "204 No Content" : "200 OK";
    const QByteArray response = "HTTP/1.1 " + statusText + "\r\nContent-Type: application/json\r\nContent-Length: " +
                                QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    socket->write(response);
    socket->disconnectFromHost();
}

}  // namespace

TEST_CASE("ClashApiClient normalizes built-in mode names for writes", "[clash]") {
    REQUIRE(tunlet::clash::toClashWriteMode("direct") == "Direct");
    REQUIRE(tunlet::clash::toClashWriteMode("global") == "Global");
    REQUIRE(tunlet::clash::toClashWriteMode("rule") == "Rule");
}

TEST_CASE("ClashApiClient preserves custom mode names for writes", "[clash]") {
    REQUIRE(tunlet::clash::toClashWriteMode("gaming") == "gaming");
    REQUIRE(tunlet::clash::toClashWriteMode("WorkMode") == "WorkMode");
}

TEST_CASE("ClashApiClient loads proxy selector state from an encoded path", "[clash]") {
    testApplication();
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost));

    QByteArray request;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
            request += socket->readAll();
            if (requestIsComplete(request)) {
                writeHttpResponse(socket, 200, R"({"now":"node-a","all":["node-a","node-b"]})");
            }
        });
    });

    tunlet::config::ClashApiConfig config;
    config.host = "127.0.0.1";
    config.port = server.serverPort();
    tunlet::clash::ClashApiClient client(1000);
    tunlet::clash::ProxySelectorStateResult result;
    QEventLoop loop;
    QObject::connect(&client,
                     &tunlet::clash::ClashApiClient::proxySelectorStateFinished,
                     &loop,
                     [&](const auto &value) {
                         result = value;
                         loop.quit();
                     });
    QTimer::singleShot(1500, &loop, &QEventLoop::quit);

    client.fetchProxySelector(config, "proxy group/group");
    loop.exec();

    REQUIRE(result.ok);
    REQUIRE(result.currentProxy == "node-a");
    REQUIRE(result.availableProxies == QStringList{"node-a", "node-b"});
    REQUIRE(request.startsWith("GET /proxies/proxy%20group%2Fgroup HTTP/1.1"));
}

TEST_CASE("ClashApiClient switches a proxy selector with PUT JSON", "[clash]") {
    testApplication();
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost));

    QByteArray request;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
            request += socket->readAll();
            if (requestIsComplete(request)) {
                writeHttpResponse(socket, 204);
            }
        });
    });

    tunlet::config::ClashApiConfig config;
    config.host = "127.0.0.1";
    config.port = server.serverPort();
    tunlet::clash::ClashApiClient client(1000);
    tunlet::clash::ProxySwitchResult result;
    QEventLoop loop;
    QObject::connect(&client,
                     &tunlet::clash::ClashApiClient::proxySwitchFinished,
                     &loop,
                     [&](const auto &value) {
                         result = value;
                         loop.quit();
                     });
    QTimer::singleShot(1500, &loop, &QEventLoop::quit);

    client.switchProxy(config, "proxy group/group", "node-b");
    loop.exec();

    REQUIRE(result.ok);
    REQUIRE(request.startsWith("PUT /proxies/proxy%20group%2Fgroup HTTP/1.1"));
    REQUIRE(request.endsWith(R"({"name":"node-b"})"));
}

TEST_CASE("ClashApiClient rejects incomplete proxy selector JSON", "[clash]") {
    testApplication();
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost));

    QByteArray request;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
            request += socket->readAll();
            if (requestIsComplete(request)) {
                writeHttpResponse(socket, 200, R"({"now":"node-a"})");
            }
        });
    });

    tunlet::config::ClashApiConfig config;
    config.host = "127.0.0.1";
    config.port = server.serverPort();
    tunlet::clash::ClashApiClient client(1000);
    tunlet::clash::ProxySelectorStateResult result;
    QEventLoop loop;
    QObject::connect(&client,
                     &tunlet::clash::ClashApiClient::proxySelectorStateFinished,
                     &loop,
                     [&](const auto &value) {
                         result = value;
                         loop.quit();
                     });
    QTimer::singleShot(1500, &loop, &QEventLoop::quit);

    client.fetchProxySelector(config, "proxy");
    loop.exec();

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.detail.contains("'all' array"));
}
