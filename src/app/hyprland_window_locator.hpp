#pragma once

#include <QString>
#include <QVector>

namespace tunlet::app {

class HyprlandWindowLocator {
public:
    struct ClientInfo {
        QString address;
        QString title;
        int workspaceId = -1;
        qint64 pid = 0;
    };

    struct Snapshot {
        bool ok = false;
        int activeWorkspaceId = -1;
        QVector<ClientInfo> currentWorkspaceClients;
        QString error;
    };

    Snapshot snapshotForCurrentProcess() const;
    bool isAvailable() const;
    bool focusWindowByAddress(const QString &address, QString *errorMessage = nullptr) const;

private:
    QString runtimeSocketPath() const;
    QByteArray query(const QByteArray &command, QString *errorMessage) const;
};

}  // namespace tunlet::app
