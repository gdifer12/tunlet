#pragma once

#include <QString>
#include <QStringList>

namespace tunlet::app {

class HyprlandWindowLocator {
public:
    struct Snapshot {
        bool ok = false;
        int activeWorkspaceId = -1;
        QStringList currentWorkspaceWindowTitles;
        QString error;
    };

    Snapshot snapshotForCurrentProcess() const;
    bool isAvailable() const;

private:
    QString runtimeSocketPath() const;
    QByteArray query(const QByteArray &command, QString *errorMessage) const;
};

}  // namespace tunlet::app
