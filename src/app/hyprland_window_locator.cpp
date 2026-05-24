#include "app/hyprland_window_locator.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

namespace tunlet::app {

QString HyprlandWindowLocator::runtimeSocketPath() const {
    const QString instanceSignature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").trimmed();
    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR").trimmed();
    if (instanceSignature.isEmpty() || runtimeDir.isEmpty()) {
        return {};
    }
    return QDir(runtimeDir).filePath(QString("hypr/%1/.socket.sock").arg(instanceSignature));
}

bool HyprlandWindowLocator::isAvailable() const {
    return !runtimeSocketPath().trimmed().isEmpty();
}

QByteArray HyprlandWindowLocator::query(const QByteArray &command, QString *errorMessage) const {
    QLocalSocket socket;
    socket.connectToServer(runtimeSocketPath());
    if (!socket.waitForConnected(200)) {
        if (errorMessage) {
            *errorMessage = socket.errorString();
        }
        return {};
    }

    socket.write(command);
    if (!socket.waitForBytesWritten(200)) {
        if (errorMessage) {
            *errorMessage = socket.errorString();
        }
        return {};
    }

    if (!socket.waitForReadyRead(500)) {
        if (errorMessage) {
            *errorMessage = socket.errorString();
        }
        return {};
    }

    QByteArray response = socket.readAll();
    while (socket.waitForReadyRead(20)) {
        response.append(socket.readAll());
    }
    return response;
}

HyprlandWindowLocator::Snapshot HyprlandWindowLocator::snapshotForCurrentProcess() const {
    Snapshot snapshot;
    if (!isAvailable()) {
        snapshot.error = "HYPRLAND_INSTANCE_SIGNATURE or XDG_RUNTIME_DIR is missing";
        return snapshot;
    }

    QString error;
    const QByteArray activeWorkspaceJson = query("j/activeworkspace", &error);
    if (activeWorkspaceJson.isEmpty()) {
        snapshot.error = error.isEmpty() ? QString("Hyprland activeworkspace query returned no data") : error;
        return snapshot;
    }
    const QJsonDocument activeWorkspaceDoc = QJsonDocument::fromJson(activeWorkspaceJson);
    if (!activeWorkspaceDoc.isObject()) {
        snapshot.error = "Hyprland activeworkspace response is not a JSON object";
        return snapshot;
    }
    snapshot.activeWorkspaceId = activeWorkspaceDoc.object().value("id").toInt(-1);
    if (snapshot.activeWorkspaceId < 0) {
        snapshot.error = "Hyprland activeworkspace response does not contain a valid id";
        return snapshot;
    }

    error.clear();
    const QByteArray clientsJson = query("j/clients", &error);
    if (clientsJson.isEmpty()) {
        snapshot.error = error.isEmpty() ? QString("Hyprland clients query returned no data") : error;
        return snapshot;
    }
    const QJsonDocument clientsDoc = QJsonDocument::fromJson(clientsJson);
    if (!clientsDoc.isArray()) {
        snapshot.error = "Hyprland clients response is not a JSON array";
        return snapshot;
    }

    const qint64 currentPid = static_cast<qint64>(QCoreApplication::applicationPid());
    for (const QJsonValue &entry : clientsDoc.array()) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject client = entry.toObject();
        if (client.value("pid").toInteger() != currentPid) {
            continue;
        }

        const QJsonObject workspace = client.value("workspace").toObject();
        if (workspace.value("id").toInt(-1) != snapshot.activeWorkspaceId) {
            continue;
        }

        const QString title = client.value("title").toString().trimmed();
        if (!title.isEmpty()) {
            snapshot.currentWorkspaceWindowTitles.push_back(title);
        }
    }

    snapshot.ok = true;
    return snapshot;
}

}  // namespace tunlet::app
