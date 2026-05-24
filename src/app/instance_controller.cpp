#include "app/instance_controller.hpp"

#include "app/application_paths.hpp"
#include "logging/logging_service.hpp"

#include <QAbstractSocket>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTextStream>

namespace tunlet::app {

namespace {

constexpr auto kOpenWindowCommand = "open_window";

QString hashPathForServerName(const QString &configPath) {
    const QString expanded = tunlet::app::expandUserPath(configPath);
    const QString normalized = QDir::cleanPath(QFileInfo(expanded).absoluteFilePath());
    const QByteArray digest = QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(digest.left(24));
}

}  // namespace

InstanceController::InstanceController(logging::LoggingService *loggingService, QObject *parent)
    : QObject(parent), m_logger(loggingService), m_server(new QLocalServer(this)) {
    connect(m_server, &QLocalServer::newConnection, this, &InstanceController::handlePendingConnections);
}

InstanceController::~InstanceController() {
    if (m_server && m_server->isListening()) {
        m_server->close();
    }
    if (!m_serverName.isEmpty()) {
        QLocalServer::removeServer(m_serverName);
    }
}

InstanceController::AcquireResult InstanceController::acquireOrForward(const QString &configPath, QString *errorMessage) {
    const QString serverName = serverNameForConfigPath(configPath);
    if (forwardCommand(serverName, kOpenWindowCommand, nullptr)) {
        if (m_logger) {
            m_logger->logInfo("app.instance",
                              "Forwarded open-window request to existing instance",
                              {},
                              {{"server_name", serverName}, {"config_path", configPath}});
        }
        return AcquireResult::ForwardedToPrimary;
    }

    if (startPrimaryServer(serverName, errorMessage)) {
        if (m_logger) {
            m_logger->logInfo("app.instance",
                              "Acquired primary instance server",
                              {},
                              {{"server_name", serverName}, {"config_path", configPath}});
        }
        return AcquireResult::Primary;
    }

    if (forwardCommand(serverName, kOpenWindowCommand, nullptr)) {
        if (m_logger) {
            m_logger->logInfo("app.instance",
                              "Forwarded open-window request after retry",
                              {},
                              {{"server_name", serverName}, {"config_path", configPath}});
        }
        return AcquireResult::ForwardedToPrimary;
    }

    return AcquireResult::Failed;
}

int InstanceController::takePendingOpenWindowRequests() {
    const int pendingCount = m_pendingOpenWindowRequests;
    m_pendingOpenWindowRequests = 0;
    return pendingCount;
}

QString InstanceController::serverNameForConfigPath(const QString &configPath) const {
    return QString("tunlet-%1").arg(hashPathForServerName(configPath));
}

bool InstanceController::forwardCommand(const QString &serverName, const QString &command, QString *errorMessage) const {
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(200)) {
        if (errorMessage) {
            *errorMessage = socket.errorString();
        }
        return false;
    }

    socket.write(command.toUtf8());
    socket.write("\n");
    if (!socket.waitForBytesWritten(200)) {
        if (errorMessage) {
            *errorMessage = socket.errorString();
        }
        socket.disconnectFromServer();
        return false;
    }

    socket.flush();
    socket.disconnectFromServer();
    return true;
}

bool InstanceController::startPrimaryServer(const QString &serverName, QString *errorMessage) {
    if (m_server->isListening()) {
        m_server->close();
    }

    if (m_server->listen(serverName)) {
        m_serverName = serverName;
        return true;
    }

    if (m_server->serverError() == QAbstractSocket::AddressInUseError) {
        QString ignoredError;
        if (!forwardCommand(serverName, kOpenWindowCommand, &ignoredError)) {
            QLocalServer::removeServer(serverName);
            if (m_server->listen(serverName)) {
                m_serverName = serverName;
                return true;
            }
        }
    }

    if (errorMessage) {
        *errorMessage = m_server->errorString();
    }
    if (m_logger) {
        m_logger->logError("app.instance",
                           "Failed to start primary instance server",
                           m_server->errorString(),
                           {{"server_name", serverName}});
    }
    return false;
}

void InstanceController::handlePendingConnections() {
    while (m_server && m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }

        auto processCommand = [this, socket]() {
            QTextStream stream(socket);
            const QString command = stream.readLine().trimmed();
            if (command == kOpenWindowCommand) {
                ++m_pendingOpenWindowRequests;
                if (m_logger) {
                    m_logger->logInfo("app.instance",
                                      "Received open-window request from secondary instance");
                }
                emit openWindowRequested();
            }
        };

        connect(socket, &QLocalSocket::readyRead, this, processCommand);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        if (socket->bytesAvailable() > 0) {
            processCommand();
        }
    }
}

}  // namespace tunlet::app
