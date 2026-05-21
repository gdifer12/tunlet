#include "clash/mode_controller.hpp"

#include "logging/logging_service.hpp"

namespace tunlet::clash {

namespace {

QString buildEndpointLabel(const config::ClashApiConfig &apiConfig) {
    return QString("%1:%2").arg(apiConfig.host).arg(apiConfig.port);
}

}  // namespace

ModeController::ModeController(const config::AppConfig &config,
                               ClashApiClient *client,
                               logging::LoggingService *loggingService,
                               QObject *parent)
    : QObject(parent), m_config(config), m_client(client), m_logger(loggingService) {
    connect(m_client, &ClashApiClient::healthCheckFinished, this, &ModeController::handleHealthResult);
    connect(m_client, &ClashApiClient::modeStateFinished, this, &ModeController::handleModeState);
    connect(m_client, &ClashApiClient::modeSwitchFinished, this, &ModeController::handleModeSwitch);
    connect(&m_modeSyncTimer, &QTimer::timeout, this, &ModeController::pollModeStatus);

    m_status.detail = "Not refreshed yet";
    m_status.endpointLabel = buildEndpointLabel(m_config.clashApi);
    configureModeSyncTimer();
}

void ModeController::refreshStatus() {
    refreshStatus(RefreshOrigin::ManualUi);
}

void ModeController::refreshStatusFromTray() {
    refreshStatus(RefreshOrigin::Tray);
}

void ModeController::refreshStatusFromConfigApply() {
    refreshStatus(RefreshOrigin::ConfigApply);
}

void ModeController::refreshStatusForStartup() {
    refreshStatus(RefreshOrigin::Startup);
}

QString ModeController::refreshOriginName(RefreshOrigin origin) const {
    switch (origin) {
    case RefreshOrigin::Tray:
        return "tray";
    case RefreshOrigin::ConfigApply:
        return "config_apply";
    case RefreshOrigin::Startup:
        return "startup";
    case RefreshOrigin::BackgroundTimer:
        return "background_timer";
    case RefreshOrigin::PostSwitchVerify:
        return "post_switch_verify";
    case RefreshOrigin::ManualUi:
    default:
        return "manual_ui";
    }
}

void ModeController::refreshStatus(RefreshOrigin origin) {
    m_status.busy = true;
    m_status.detail = "Refreshing status...";
    emit statusUpdated(m_status);

    if (m_logger) {
        m_logger->logInfo("mode.sync",
                          "Started Clash mode refresh",
                          {},
                          {{"origin", refreshOriginName(origin)}, {"endpoint", m_status.endpointLabel}});
    }

    m_client->checkHealth(m_config.clashApi);
    m_client->fetchCurrentMode(m_config.clashApi);
}

void ModeController::switchMode(const QString &profileName) {
    const auto *profile = findProfile(profileName);
    if (!profile) {
        if (m_logger) {
            m_logger->logWarning("mode.switch",
                                 "Rejected mode switch for unknown profile",
                                 QString("unknown mode profile: %1").arg(profileName),
                                 {{"requested_profile", profileName}});
        }
        emit operationFailed(QString("unknown mode profile: %1").arg(profileName));
        return;
    }

    if (m_logger) {
        m_logger->logInfo("mode.switch",
                          "Requested mode switch",
                          QString("Switching to %1").arg(profile->name),
                          {{"requested_profile", profile->name}, {"backend_mode", profile->mode}});
    }

    m_status.busy = true;
    m_status.switchInFlight = true;
    m_status.detail = QString("Switching to %1...").arg(profile->name);
    m_pendingProfileName = profile->name;
    m_pendingModeValue = profile->mode;
    emit statusUpdated(m_status);

    m_client->switchMode(m_config.clashApi, profile->mode);
}

void ModeController::updateConfig(const config::AppConfig &config) {
    m_config = config;
    m_status.endpointLabel = buildEndpointLabel(m_config.clashApi);
    configureModeSyncTimer();
    if (!findProfile(m_pendingProfileName)) {
        m_pendingProfileName.clear();
        m_pendingModeValue.clear();
        m_status.switchInFlight = false;
    }
    emit profilesUpdated(m_config.clashApi.profiles);
    emit statusUpdated(m_status);
}

ModeStatus ModeController::status() const {
    return m_status;
}

QVector<config::ClashModeProfile> ModeController::profiles() const {
    return m_config.clashApi.profiles;
}

void ModeController::configureModeSyncTimer() {
    const bool wasActive = m_modeSyncTimer.isActive();
    const int previousIntervalMs = m_modeSyncTimer.interval();
    if (m_config.clashApi.modeSyncIntervalMs <= 0) {
        m_modeSyncTimer.stop();
        if (m_logger && (wasActive || previousIntervalMs > 0)) {
            m_logger->logInfo("mode.sync",
                              "Disabled background mode sync timer",
                              {},
                              {{"endpoint", m_status.endpointLabel}});
        }
        return;
    }

    m_modeSyncTimer.setInterval(m_config.clashApi.modeSyncIntervalMs);
    if (!m_modeSyncTimer.isActive()) {
        m_modeSyncTimer.start();
    }
    if (m_logger &&
        (!wasActive || previousIntervalMs != m_config.clashApi.modeSyncIntervalMs)) {
        m_logger->logInfo("mode.sync",
                          wasActive ? "Updated background mode sync timer interval"
                                    : "Enabled background mode sync timer",
                          {},
                          {{"endpoint", m_status.endpointLabel},
                           {"interval_ms", QString::number(m_config.clashApi.modeSyncIntervalMs)}});
    }
}

void ModeController::pollModeStatus() {
    if (m_status.busy) {
        return;
    }
    refreshStatus(RefreshOrigin::BackgroundTimer);
}

void ModeController::handleHealthResult(const HealthCheckResult &result) {
    m_status.reachable = result.ok;
    if (!result.ok) {
        m_status.detail = result.detail;
        m_status.lastUpdated = QDateTime::currentDateTime();
        m_status.busy = false;
        emit statusUpdated(m_status);
    }
}

void ModeController::handleModeState(const ModeStateResult &result) {
    const QString previousMode = m_status.currentModeValue;
    const QString previousProfile = m_status.currentProfileName;
    const QString previousDetail = m_status.detail;
    const bool wasReachable = m_status.reachable;
    const QString pendingModeValue = m_pendingModeValue;
    const QString pendingProfileName = m_pendingProfileName;

    m_status.busy = false;
    m_status.switchInFlight = false;
    m_status.lastUpdated = QDateTime::currentDateTime();
    if (!result.ok) {
        m_status.detail = result.detail;
        m_status.currentProfileName.clear();
        m_status.currentModeValue.clear();
        if (m_logger &&
            (wasReachable || !previousMode.isEmpty() || previousDetail != result.detail)) {
            m_logger->logWarning("mode.sync",
                                 "Failed to refresh Clash mode state",
                                 result.detail,
                                 {{"endpoint", m_status.endpointLabel}});
        }
        emit statusUpdated(m_status);
        return;
    }

    m_status.reachable = true;
    m_status.currentModeValue = result.currentMode;
    m_status.supportedModes = result.supportedModes;
    m_status.currentProfileName = profileNameForModeValue(result.currentMode);

    if (!m_pendingModeValue.isEmpty()) {
        if (QString::compare(result.currentMode, m_pendingModeValue, Qt::CaseInsensitive) == 0) {
            m_status.detail = QString("Current mode: %1").arg(result.currentMode);
            if (m_logger) {
                m_logger->logInfo("mode.switch",
                                  "Backend mode switch confirmed",
                                  m_status.detail,
                                  {{"profile", pendingProfileName}, {"backend_mode", result.currentMode}});
            }
        } else {
            m_status.detail =
                QString("Switch request did not apply: API still reports '%1' instead of '%2'")
                    .arg(result.currentMode, m_pendingModeValue);
            if (m_logger) {
                m_logger->logError("mode.switch",
                                   "Mode switch verification failed",
                                   m_status.detail,
                                   {{"requested_profile", pendingProfileName},
                                    {"expected_backend_mode", pendingModeValue},
                                    {"reported_backend_mode", result.currentMode}});
            }
            emit statusUpdated(m_status);
            emit operationFailed(m_status.detail);
            m_pendingProfileName.clear();
            m_pendingModeValue.clear();
            return;
        }
    } else {
        m_status.detail = QString("Current mode: %1").arg(result.currentMode);
        const bool modeChanged = QString::compare(previousMode, result.currentMode, Qt::CaseInsensitive) != 0;
        const bool profileChanged = previousProfile != m_status.currentProfileName;
        if (m_logger && (modeChanged || profileChanged || !wasReachable)) {
            m_logger->logInfo("mode.sync",
                              modeChanged || profileChanged ? "Observed backend mode change"
                                                            : "Recovered Clash mode synchronization",
                              m_status.detail,
                              {{"profile", m_status.currentProfileName},
                               {"backend_mode", result.currentMode},
                               {"endpoint", m_status.endpointLabel}});
        }
    }

    m_pendingProfileName.clear();
    m_pendingModeValue.clear();
    emit statusUpdated(m_status);
}

void ModeController::handleModeSwitch(const ModeSwitchResult &result) {
    m_status.busy = false;
    m_status.lastUpdated = QDateTime::currentDateTime();
    if (!result.ok) {
        m_status.switchInFlight = false;
        m_status.detail = result.detail;
        m_pendingProfileName.clear();
        m_pendingModeValue.clear();
        if (m_logger) {
            m_logger->logError("mode.switch",
                               "Mode switch request failed",
                               result.detail,
                               {{"endpoint", m_status.endpointLabel}});
        }
        emit statusUpdated(m_status);
        emit operationFailed(result.detail);
        return;
    }

    m_status.detail = result.detail;
    if (m_logger) {
        m_logger->logInfo("mode.switch",
                          "Mode switch request accepted by Clash API",
                          result.detail,
                          {{"endpoint", m_status.endpointLabel}});
    }
    emit statusUpdated(m_status);
    refreshStatus(RefreshOrigin::PostSwitchVerify);
}

QString ModeController::profileNameForModeValue(const QString &modeValue) const {
    for (const auto &profile : m_config.clashApi.profiles) {
        if (QString::compare(profile.mode, modeValue, Qt::CaseInsensitive) == 0) {
            return profile.name;
        }
    }
    return QString("unknown");
}

const config::ClashModeProfile *ModeController::findProfile(const QString &profileName) const {
    for (const auto &profile : m_config.clashApi.profiles) {
        if (profile.name == profileName) {
            return &profile;
        }
    }
    return nullptr;
}

}  // namespace tunlet::clash
