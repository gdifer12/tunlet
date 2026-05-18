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
    int modeSyncIntervalMs = 0;
    bool disableDefaultProfiles = false;
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

enum class DiagnosticsLocationMode {
    Disabled,
    LocalDb,
    DynamicCache,
};

struct DiagnosticsLocationLocalDbConfig {
    QString databasePath;
    QString asnDatabasePath;
    QString downloadUrl;
};

struct DiagnosticsLocationDynamicCacheConfig {
    QString provider = "ipwhois";
    QString url = "https://ipwho.is/";
    QString cachePath;
    int baseRefreshDays = 14;
    int randomShiftDays = 3;
    int timeoutMs = 5000;
    bool refreshOnStartup = false;
    bool allowManualRefresh = true;
};

struct DiagnosticsLocationConfig {
    DiagnosticsLocationMode mode = DiagnosticsLocationMode::LocalDb;
    DiagnosticsLocationLocalDbConfig localDb;
    DiagnosticsLocationDynamicCacheConfig dynamicCache;
};

struct DiagnosticsConnectionConfig {
    DiagnosticsCommandConfig ipv4;
    DiagnosticsCommandConfig timing;
    DiagnosticsCommandConfig dns;
    DiagnosticsLocationConfig location;
};

struct DiagnosticsConfig {
    bool enabled = true;
    int refreshIntervalMs = 180000;
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

struct TrayConfig {
    bool keepRunningWithoutWindow = true;
    bool startHidden = false;
};

struct UiTextSelectionConfig {
    bool enableInformationalLabels = true;
};

struct UiKeyboardShortcutsConfig {
    QString closeWindowPrimary = "Esc";
    QString closeWindowSecondary = "Q";
    QString nextPage = "Ctrl+Tab";
    QString previousPage = "Ctrl+Shift+Tab";
    QString pageMain = "1";
    QString pageRules = "2";
    QString pageSettings = "3";
    QString openModeSelector = "M";
    QString openRuleFileSelector = "R";
    QString refreshRuntime = "F5";
    QString refreshLocationData = "Shift+F5";
    QString validateEditor = "Ctrl+Shift+V";
    QString saveEditor = "Ctrl+S";
    QString reloadEditor = "Ctrl+R";
};

struct UiKeyboardConfig {
    UiKeyboardShortcutsConfig shortcuts;
};

struct UiConfig {
    UiTextSelectionConfig textSelection;
    UiKeyboardConfig keyboard;
};

struct AppConfig {
    QString configRoute;
    ClashApiConfig clashApi;
    RuleSetPathsConfig ruleSets;
    DiagnosticsConfig diagnostics;
    ThemeConfig theme;
    EditingConfig editing;
    TrayConfig tray;
    UiConfig ui;
    QString configPath;
};

}  // namespace tunlet::config
