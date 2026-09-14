#pragma once

#include "clash/clash_api_client.hpp"
#include "config/app_config.hpp"
#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

namespace tunlet::logging {
class LoggingService;
}

namespace tunlet::clash {

struct ModeOption {
    QString id;
    QString name;
    QString mode;
    QString desc;
};

struct ProxySelectorStatus {
    bool enabled = false;
    QString selectorName;
    QString currentProxy;
    QStringList availableProxies;
    bool reachable = false;
    bool busy = false;
    bool switchInFlight = false;
    bool stale = false;
    QString detail;
    QDateTime lastUpdated;
};

struct ModeStatus {
    QString currentProfileId;
    QString currentProfileName;
    QString currentModeValue;
    QString endpointLabel;
    QStringList supportedModes;
    bool reachable = false;
    bool busy = false;
    bool switchInFlight = false;
    QString detail;
    QDateTime lastUpdated;
    ProxySelectorStatus proxySelector;
};

class ModeController : public QObject {
    Q_OBJECT

public:
    ModeController(const config::AppConfig &config,
                   ClashApiClient *client,
                   logging::LoggingService *loggingService = nullptr,
                   QObject *parent = nullptr);

    void refreshStatus();
    void refreshStatusFromTray();
    void refreshStatusFromConfigApply();
    void refreshStatusForStartup();
    void switchMode(const QString &optionId);
    void switchProxy(const QString &proxyName);
    void updateConfig(const config::AppConfig &config);

    ModeStatus status() const;
    QVector<ModeOption> profiles() const;

signals:
    void statusUpdated(const tunlet::clash::ModeStatus &status);
    void profilesUpdated(const QVector<tunlet::clash::ModeOption> &profiles);
    void modeOperationFailed(const QString &message);
    void proxyOperationFailed(const QString &message);

private:
    enum class RefreshOrigin {
        ManualUi,
        Tray,
        ConfigApply,
        Startup,
        BackgroundTimer,
        PostSwitchVerify,
    };

    QString refreshOriginName(RefreshOrigin origin) const;
    void refreshStatus(RefreshOrigin origin);
    void configureModeSyncTimer();
    void pollModeStatus();
    void handleHealthResult(const HealthCheckResult &result);
    void handleModeState(const ModeStateResult &result);
    void handleModeSwitch(const ModeSwitchResult &result);
    void handleProxySelectorState(const ProxySelectorStateResult &result);
    void handleProxySwitch(const ProxySwitchResult &result);
    bool rebuildModeOptions();
    const ModeOption *optionForModeValue(const QString &modeValue) const;
    const ModeOption *findProfile(const QString &optionId) const;

    config::AppConfig m_config;
    ClashApiClient *m_client = nullptr;
    logging::LoggingService *m_logger = nullptr;
    ModeStatus m_status;
    QString m_pendingProfileName;
    QString m_pendingModeValue;
    QString m_pendingProxyName;
    QVector<ModeOption> m_modeOptions;
    QTimer m_modeSyncTimer;
};

}  // namespace tunlet::clash
