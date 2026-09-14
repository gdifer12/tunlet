#include "clash/mode_controller.hpp"

#include "logging/logging_service.hpp"

#include <QSet>

namespace tunlet::clash {

namespace {

QString buildEndpointLabel(const config::ClashApiConfig &apiConfig) {
    return QString("%1:%2").arg(apiConfig.host).arg(apiConfig.port);
}

QString normalizedValue(const QString &value) {
    return value.trimmed().toCaseFolded();
}

bool containsMode(const QStringList &modes, const QString &mode) {
    const QString normalizedMode = normalizedValue(mode);
    for (const auto &candidate : modes) {
        if (normalizedValue(candidate) == normalizedMode) {
            return true;
        }
    }
    return false;
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
    connect(m_client,
            &ClashApiClient::proxySelectorStateFinished,
            this,
            &ModeController::handleProxySelectorState);
    connect(m_client, &ClashApiClient::proxySwitchFinished, this, &ModeController::handleProxySwitch);
    connect(&m_modeSyncTimer, &QTimer::timeout, this, &ModeController::pollModeStatus);

    m_status.detail = "Not refreshed yet";
    m_status.endpointLabel = buildEndpointLabel(m_config.clashApi);
    m_status.proxySelector.enabled = m_config.clashApi.editProxySelector;
    m_status.proxySelector.selectorName = m_config.clashApi.proxySelector;
    m_status.proxySelector.detail = m_status.proxySelector.enabled ? "Not refreshed yet" : "Proxy selector disabled";
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
    if (m_status.proxySelector.enabled) {
        m_status.proxySelector.busy = true;
        m_status.proxySelector.detail = "Refreshing proxy selector...";
    }
    emit statusUpdated(m_status);

    if (m_logger) {
        m_logger->logInfo("mode.sync",
                          "Started Clash mode refresh",
                          {},
                          {{"origin", refreshOriginName(origin)}, {"endpoint", m_status.endpointLabel}});
    }

    m_client->checkHealth(m_config.clashApi);
    m_client->fetchCurrentMode(m_config.clashApi);
    if (m_status.proxySelector.enabled) {
        m_client->fetchProxySelector(m_config.clashApi, m_status.proxySelector.selectorName);
    }
}

void ModeController::switchMode(const QString &optionId) {
    const auto *profile = findProfile(optionId);
    if (!profile || m_status.busy || m_status.proxySelector.busy) {
        const QString detail = !profile ? QString("unknown mode option: %1").arg(optionId)
                                        : QString("another runtime control operation is already in progress");
        if (m_logger) {
            m_logger->logWarning("mode.switch",
                                 "Rejected mode switch request",
                                 detail,
                                 {{"requested_option", optionId}});
        }
        emit modeOperationFailed(detail);
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

void ModeController::switchProxy(const QString &proxyName) {
    auto &proxy = m_status.proxySelector;
    const QString targetProxy = proxyName.trimmed();
    if (!proxy.enabled || !proxy.reachable || m_status.busy || proxy.busy || targetProxy.isEmpty() ||
        !proxy.availableProxies.contains(targetProxy)) {
        const QString detail = QString("proxy '%1' is not available for selector '%2'")
                                   .arg(targetProxy, proxy.selectorName);
        if (m_logger) {
            m_logger->logWarning("proxy.switch",
                                 "Rejected proxy switch request",
                                 detail,
                                 {{"selector", proxy.selectorName}, {"requested_proxy", targetProxy}});
        }
        emit proxyOperationFailed(detail);
        return;
    }
    if (targetProxy == proxy.currentProxy) {
        return;
    }

    proxy.busy = true;
    proxy.switchInFlight = true;
    proxy.detail = QString("Switching to %1...").arg(targetProxy);
    m_pendingProxyName = targetProxy;
    if (m_logger) {
        m_logger->logInfo("proxy.switch",
                          "Requested proxy selector switch",
                          proxy.detail,
                          {{"selector", proxy.selectorName}, {"requested_proxy", targetProxy}});
    }
    emit statusUpdated(m_status);
    m_client->switchProxy(m_config.clashApi, proxy.selectorName, targetProxy);
}

void ModeController::updateConfig(const config::AppConfig &config) {
    const bool endpointChanged = m_config.clashApi.host != config.clashApi.host ||
                                 m_config.clashApi.port != config.clashApi.port;
    const bool selectorChanged = endpointChanged ||
                                 m_config.clashApi.proxySelector != config.clashApi.proxySelector;
    m_config = config;
    m_status.endpointLabel = buildEndpointLabel(m_config.clashApi);
    if (endpointChanged) {
        m_status.currentProfileId.clear();
        m_status.currentProfileName.clear();
        m_status.currentModeValue.clear();
        m_status.supportedModes.clear();
        m_status.reachable = false;
        m_status.detail = "Not refreshed yet";
    }
    if (selectorChanged || !m_config.clashApi.editProxySelector) {
        m_status.proxySelector = {};
    }
    m_status.proxySelector.enabled = m_config.clashApi.editProxySelector;
    m_status.proxySelector.selectorName = m_config.clashApi.proxySelector;
    if (!m_status.proxySelector.enabled) {
        m_status.proxySelector.detail = "Proxy selector disabled";
    }
    configureModeSyncTimer();
    m_pendingProfileName.clear();
    m_pendingModeValue.clear();
    m_status.switchInFlight = false;
    m_pendingProxyName.clear();
    m_status.proxySelector.switchInFlight = false;
    const bool profilesChanged = rebuildModeOptions();
    const ModeOption *activeOption = optionForModeValue(m_status.currentModeValue);
    m_status.currentProfileId = activeOption ? activeOption->id : QString{};
    m_status.currentProfileName = activeOption ? activeOption->name
                                               : (m_status.currentModeValue.isEmpty() ? QString{} : QString("unknown"));
    if (profilesChanged) {
        emit profilesUpdated(m_modeOptions);
    }
    emit statusUpdated(m_status);
}

ModeStatus ModeController::status() const {
    return m_status;
}

QVector<ModeOption> ModeController::profiles() const {
    return m_modeOptions;
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
    if (m_status.busy || m_status.proxySelector.busy) {
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
        m_status.currentProfileId.clear();
        m_status.currentProfileName.clear();
        m_status.currentModeValue.clear();
        const bool switchVerificationFailed = !pendingModeValue.isEmpty();
        m_pendingProfileName.clear();
        m_pendingModeValue.clear();
        if (m_logger &&
            (wasReachable || !previousMode.isEmpty() || previousDetail != result.detail)) {
            m_logger->logWarning("mode.sync",
                                 "Failed to refresh Clash mode state",
                                 result.detail,
                                 {{"endpoint", m_status.endpointLabel}});
        }
        emit statusUpdated(m_status);
        if (switchVerificationFailed) {
            emit modeOperationFailed(result.detail);
        }
        return;
    }

    m_status.reachable = true;
    m_status.currentModeValue = result.currentMode;
    m_status.supportedModes = result.supportedModes;
    const bool profilesChanged = rebuildModeOptions();
    const ModeOption *activeOption = optionForModeValue(result.currentMode);
    m_status.currentProfileId = activeOption ? activeOption->id : QString{};
    m_status.currentProfileName = activeOption ? activeOption->name : QString("unknown");
    if (profilesChanged) {
        emit profilesUpdated(m_modeOptions);
    }

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
            emit modeOperationFailed(m_status.detail);
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
        emit modeOperationFailed(result.detail);
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

void ModeController::handleProxySelectorState(const ProxySelectorStateResult &result) {
    auto &proxy = m_status.proxySelector;
    if (!proxy.enabled || result.selectorName != proxy.selectorName) {
        return;
    }

    const QString previousProxy = proxy.currentProxy;
    const QString previousDetail = proxy.detail;
    const bool wasReachable = proxy.reachable;
    const QString pendingProxy = m_pendingProxyName;

    proxy.busy = false;
    proxy.switchInFlight = false;
    proxy.lastUpdated = QDateTime::currentDateTime();
    if (!result.ok) {
        proxy.reachable = false;
        proxy.stale = !proxy.currentProxy.isEmpty() || !proxy.availableProxies.isEmpty();
        proxy.detail = result.detail;
        m_pendingProxyName.clear();
        if (m_logger && (wasReachable || previousDetail != result.detail)) {
            m_logger->logWarning("proxy.sync",
                                 "Failed to refresh proxy selector state",
                                 result.detail,
                                 {{"selector", proxy.selectorName}, {"endpoint", m_status.endpointLabel}});
        }
        emit statusUpdated(m_status);
        if (!pendingProxy.isEmpty()) {
            emit proxyOperationFailed(result.detail);
        }
        return;
    }

    proxy.reachable = true;
    proxy.stale = false;
    proxy.currentProxy = result.currentProxy;
    proxy.availableProxies = result.availableProxies;

    if (!pendingProxy.isEmpty()) {
        if (result.currentProxy == pendingProxy) {
            proxy.detail = QString("Current proxy: %1").arg(result.currentProxy);
            if (m_logger) {
                m_logger->logInfo("proxy.switch",
                                  "Proxy selector switch confirmed",
                                  proxy.detail,
                                  {{"selector", proxy.selectorName}, {"proxy", result.currentProxy}});
            }
        } else {
            proxy.detail = QString("Proxy switch did not apply: selector reports '%1' instead of '%2'")
                               .arg(result.currentProxy, pendingProxy);
            if (m_logger) {
                m_logger->logError("proxy.switch",
                                   "Proxy selector switch verification failed",
                                   proxy.detail,
                                   {{"selector", proxy.selectorName},
                                    {"expected_proxy", pendingProxy},
                                    {"reported_proxy", result.currentProxy}});
            }
            m_pendingProxyName.clear();
            emit statusUpdated(m_status);
            emit proxyOperationFailed(proxy.detail);
            return;
        }
    } else {
        proxy.detail = QString("Current proxy: %1").arg(result.currentProxy);
        if (m_logger && (!wasReachable || previousProxy != result.currentProxy)) {
            m_logger->logInfo("proxy.sync",
                              previousProxy != result.currentProxy ? "Observed proxy selector change"
                                                                   : "Recovered proxy selector synchronization",
                              proxy.detail,
                              {{"selector", proxy.selectorName},
                               {"proxy", result.currentProxy},
                               {"endpoint", m_status.endpointLabel}});
        }
    }

    m_pendingProxyName.clear();
    emit statusUpdated(m_status);
}

void ModeController::handleProxySwitch(const ProxySwitchResult &result) {
    auto &proxy = m_status.proxySelector;
    if (!proxy.enabled || result.selectorName != proxy.selectorName) {
        return;
    }

    if (!result.ok) {
        proxy.busy = false;
        proxy.switchInFlight = false;
        proxy.detail = result.detail;
        m_pendingProxyName.clear();
        if (m_logger) {
            m_logger->logError("proxy.switch",
                               "Proxy selector switch request failed",
                               result.detail,
                               {{"selector", proxy.selectorName},
                                {"requested_proxy", result.targetProxy},
                                {"endpoint", m_status.endpointLabel}});
        }
        emit statusUpdated(m_status);
        emit proxyOperationFailed(result.detail);
        return;
    }

    proxy.busy = true;
    proxy.switchInFlight = true;
    proxy.detail = result.detail;
    if (m_logger) {
        m_logger->logInfo("proxy.switch",
                          "Proxy selector switch request accepted",
                          result.detail,
                          {{"selector", proxy.selectorName}, {"requested_proxy", result.targetProxy}});
    }
    emit statusUpdated(m_status);
    m_client->fetchProxySelector(m_config.clashApi, proxy.selectorName);
}

bool ModeController::rebuildModeOptions() {
    QVector<ModeOption> options;
    QSet<QString> representedModes;

    for (const auto &profile : m_config.clashApi.profiles) {
        if (!containsMode(m_status.supportedModes, profile.mode)) {
            continue;
        }
        options.push_back({QString("configured:%1").arg(profile.name), profile.name, profile.mode, profile.desc});
        representedModes.insert(normalizedValue(profile.mode));
    }

    if (m_config.clashApi.displayAllModes) {
        for (const auto &mode : m_status.supportedModes) {
            const QString normalizedMode = normalizedValue(mode);
            if (normalizedMode.isEmpty() || representedModes.contains(normalizedMode)) {
                continue;
            }
            options.push_back({QString("runtime:%1").arg(mode),
                               mode,
                               mode,
                               "Discovered from the Clash API mode-list"});
            representedModes.insert(normalizedMode);
        }
    }

    bool changed = options.size() != m_modeOptions.size();
    if (!changed) {
        for (qsizetype index = 0; index < options.size(); ++index) {
            const auto &next = options.at(index);
            const auto &current = m_modeOptions.at(index);
            if (next.id != current.id || next.name != current.name || next.mode != current.mode ||
                next.desc != current.desc) {
                changed = true;
                break;
            }
        }
    }
    m_modeOptions = options;
    return changed;
}

const ModeOption *ModeController::optionForModeValue(const QString &modeValue) const {
    for (const auto &option : m_modeOptions) {
        if (QString::compare(option.mode, modeValue, Qt::CaseInsensitive) == 0) {
            return &option;
        }
    }
    return nullptr;
}

const ModeOption *ModeController::findProfile(const QString &optionId) const {
    for (const auto &profile : m_modeOptions) {
        if (profile.id == optionId) {
            return &profile;
        }
    }
    return nullptr;
}

}  // namespace tunlet::clash
