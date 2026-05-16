#include "clash/mode_controller.hpp"

namespace tunlet::clash {

namespace {

QString buildEndpointLabel(const config::ClashApiConfig &apiConfig) {
    return QString("%1:%2").arg(apiConfig.host).arg(apiConfig.port);
}

}  // namespace

ModeController::ModeController(const config::AppConfig &config, ClashApiClient *client, QObject *parent)
    : QObject(parent), m_config(config), m_client(client) {
    connect(m_client, &ClashApiClient::healthCheckFinished, this, &ModeController::handleHealthResult);
    connect(m_client, &ClashApiClient::modeStateFinished, this, &ModeController::handleModeState);
    connect(m_client, &ClashApiClient::modeSwitchFinished, this, &ModeController::handleModeSwitch);

    m_status.detail = "Not refreshed yet";
    m_status.endpointLabel = buildEndpointLabel(m_config.clashApi);
}

void ModeController::refreshStatus() {
    m_status.busy = true;
    m_status.detail = "Refreshing status...";
    emit statusUpdated(m_status);

    m_client->checkHealth(m_config.clashApi);
    m_client->fetchCurrentMode(m_config.clashApi);
}

void ModeController::switchMode(const QString &profileName) {
    const auto *profile = findProfile(profileName);
    if (!profile) {
        emit operationFailed(QString("unknown mode profile: %1").arg(profileName));
        return;
    }

    m_status.busy = true;
    m_status.detail = QString("Switching to %1...").arg(profile->name);
    m_pendingProfileName = profile->name;
    m_pendingModeValue = profile->mode;
    emit statusUpdated(m_status);

    m_client->switchMode(m_config.clashApi, profile->mode);
}

void ModeController::updateConfig(const config::AppConfig &config) {
    m_config = config;
    m_status.endpointLabel = buildEndpointLabel(m_config.clashApi);
    if (!findProfile(m_pendingProfileName)) {
        m_pendingProfileName.clear();
        m_pendingModeValue.clear();
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
    m_status.busy = false;
    m_status.lastUpdated = QDateTime::currentDateTime();
    if (!result.ok) {
        m_status.detail = result.detail;
        m_status.currentProfileName.clear();
        m_status.currentModeValue.clear();
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
        } else {
            m_status.detail =
                QString("Switch request did not apply: API still reports '%1' instead of '%2'")
                    .arg(result.currentMode, m_pendingModeValue);
            emit statusUpdated(m_status);
            emit operationFailed(m_status.detail);
            m_pendingProfileName.clear();
            m_pendingModeValue.clear();
            return;
        }
    } else {
        m_status.detail = QString("Current mode: %1").arg(result.currentMode);
    }

    m_pendingProfileName.clear();
    m_pendingModeValue.clear();
    emit statusUpdated(m_status);
}

void ModeController::handleModeSwitch(const ModeSwitchResult &result) {
    m_status.busy = false;
    m_status.lastUpdated = QDateTime::currentDateTime();
    if (!result.ok) {
        m_status.detail = result.detail;
        m_pendingProfileName.clear();
        m_pendingModeValue.clear();
        emit statusUpdated(m_status);
        emit operationFailed(result.detail);
        return;
    }

    m_status.detail = result.detail;
    emit statusUpdated(m_status);
    refreshStatus();
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
