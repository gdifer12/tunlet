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

struct ModeStatus {
    QString currentProfileName;
    QString currentModeValue;
    QString endpointLabel;
    QStringList supportedModes;
    bool reachable = false;
    bool busy = false;
    bool switchInFlight = false;
    QString detail;
    QDateTime lastUpdated;
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
    void switchMode(const QString &profileName);
    void updateConfig(const config::AppConfig &config);

    ModeStatus status() const;
    QVector<config::ClashModeProfile> profiles() const;

signals:
    void statusUpdated(const tunlet::clash::ModeStatus &status);
    void profilesUpdated(const QVector<tunlet::config::ClashModeProfile> &profiles);
    void operationFailed(const QString &message);

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
    QString profileNameForModeValue(const QString &modeValue) const;
    const config::ClashModeProfile *findProfile(const QString &profileName) const;

    config::AppConfig m_config;
    ClashApiClient *m_client = nullptr;
    logging::LoggingService *m_logger = nullptr;
    ModeStatus m_status;
    QString m_pendingProfileName;
    QString m_pendingModeValue;
    QTimer m_modeSyncTimer;
};

}  // namespace tunlet::clash
