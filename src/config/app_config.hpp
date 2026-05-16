#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace tunlet::config {

struct ClashModeProfile {
    QString name;
    QString mode;
    QString desc;
};

struct ClashApiConfig {
    QString host = "127.0.0.1";
    quint16 port = 0;
    QVector<ClashModeProfile> profiles;
};

struct RuleSetFileConfig {
    QString name;
    QString path;
    QString description;
};

struct RuleSetPathsConfig {
    QString forceProxyPath;
    QString forceDirectPath;
    QString autoProxyPath;
    QString autoDirectPath;
    QVector<RuleSetFileConfig> extraFiles;
};

struct DiagnosticsCommandConfig {
    QString executable;
    QStringList args;
};

struct DiagnosticsLocationConfig {
    bool enabled = true;
    QString databasePath;
};

struct DiagnosticsConnectionConfig {
    DiagnosticsCommandConfig ipv4;
    DiagnosticsCommandConfig timing;
    DiagnosticsCommandConfig dns;
    DiagnosticsLocationConfig location;
};

struct DiagnosticsConfig {
    bool enabled = true;
    int refreshIntervalMs = 10000;
    int requestTimeoutMs = 5000;
    DiagnosticsConnectionConfig connection;
};

struct ThemeConfig {
    QString qssPath;
};

struct EditingConfig {
    bool createBackup = true;
    QString backupSuffix = ".bak";
};

struct AppConfig {
    ClashApiConfig clashApi;
    RuleSetPathsConfig ruleSets;
    DiagnosticsConfig diagnostics;
    ThemeConfig theme;
    EditingConfig editing;
    QString configPath;
};

}  // namespace tunlet::config
