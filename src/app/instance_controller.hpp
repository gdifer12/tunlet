#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

namespace tunlet::logging {
class LoggingService;
}

namespace tunlet::app {

class InstanceController : public QObject {
    Q_OBJECT

public:
    enum class AcquireResult {
        Primary,
        ForwardedToPrimary,
        Failed,
    };

    explicit InstanceController(logging::LoggingService *loggingService = nullptr, QObject *parent = nullptr);
    ~InstanceController() override;

    AcquireResult acquireOrForward(const QString &configPath, QString *errorMessage = nullptr);
    int takePendingOpenWindowRequests();

signals:
    void openWindowRequested();

private:
    QString serverNameForConfigPath(const QString &configPath) const;
    bool forwardCommand(const QString &serverName, const QString &command, QString *errorMessage) const;
    bool startPrimaryServer(const QString &serverName, QString *errorMessage);
    void handlePendingConnections();

    logging::LoggingService *m_logger = nullptr;
    QLocalServer *m_server = nullptr;
    QString m_serverName;
    int m_pendingOpenWindowRequests = 0;
};

}  // namespace tunlet::app
