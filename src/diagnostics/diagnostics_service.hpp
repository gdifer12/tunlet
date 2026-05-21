#pragma once

#include "clash/clash_api_client.hpp"
#include "config/app_config.hpp"
#include "diagnostics/geoip_provider.hpp"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include <memory>

class QNetworkReply;

namespace tunlet::clash {
struct ModeStatus;
}

namespace tunlet::logging {
class LoggingService;
}

namespace tunlet::diagnostics {

struct DiagnosticsSnapshot {
    bool apiReachable = false;
    QString apiDetail = "No diagnostics yet";
    QString publicIp;
    QString publicIpDetail = "Public IP unavailable";
    QString location;
    QString locationCountryCode;
    QString locationDetail = "Location unavailable";
    QString locationSource = "Source unavailable";
    QString locationAsnOrg = "ASN / Org unavailable";
    QDateTime locationUpdatedAt;
    QDateTime locationNextRefreshAt;
    bool locationStale = false;
    bool locationDisabled = false;
    qint64 delayDnsMs = -1;
    qint64 delayConnectMs = -1;
    qint64 delayTlsMs = -1;
    qint64 delayTotalMs = -1;
    QString delayDetail = "Delay unavailable";
    QString dnsSummary = "DNS unavailable";
    QString dnsDetail = "DNS lookup unavailable";
    bool trafficAvailable = false;
    QString trafficSummary = "Unavailable";
    QString trafficDetail = "Traffic metrics unavailable";
    QString configurationSummary;
    QString configurationDetail;
    QString externalDetail;
    bool runtimeRefreshInFlight = false;
    bool locationRefreshInFlight = false;
    bool runtimeDiagnosticsStale = false;
    QDateTime lastSuccessfulRuntimeRefreshAt;
    QString lastRuntimeRefreshFailureDetail;
    QDateTime lastUpdated;
};

class DiagnosticsService : public QObject {
    Q_OBJECT

public:
    DiagnosticsService(const config::AppConfig &config,
                       clash::ClashApiClient *client,
                       logging::LoggingService *loggingService = nullptr,
                       QObject *parent = nullptr);

    void start();
    void refreshNow();
    void refreshFromTray();
    void refreshFromConfigApply();
    void refreshLocationDataNow();
    void updateConfig(const config::AppConfig &config);
    void observeModeStatus(const tunlet::clash::ModeStatus &status);
    DiagnosticsSnapshot snapshot() const;

signals:
    void diagnosticsUpdated(const tunlet::diagnostics::DiagnosticsSnapshot &snapshot);

private:
    enum class RefreshOrigin {
        RuntimeReread,
        TrayInteractive,
        ConfigApply,
        PeriodicTimer,
        StartupBootstrap,
        ModeChangeBootstrap,
    };

    enum class RuntimeProbeKind {
        PublicIp,
        Delay,
        Dns,
    };

    struct RuntimeRefreshProgress {
        quint64 generation = 0;
        bool active = false;
        RefreshOrigin origin = RefreshOrigin::RuntimeReread;
        bool publicIpDone = false;
        bool delayDone = false;
        bool dnsDone = false;
        bool publicIpOk = false;
        bool delayOk = false;
        bool dnsOk = false;
        QString publicIpFailure;
        QString delayFailure;
        QString dnsFailure;
    };

    void handleTrafficResult(const clash::TrafficResult &result);
    void handleHealthResult(const clash::HealthCheckResult &result);
    void updateConfigurationSnapshot();
    void resetConnectionSnapshot(const QString &reason);
    void abortProbe(QPointer<QProcess> &process);
    QString refreshOriginName(RefreshOrigin origin) const;
    void refreshFromPeriodicTimer();
    void markRuntimeRefreshStarted(quint64 generation, RefreshOrigin origin);
    void noteRuntimeProbeResult(quint64 generation, RuntimeProbeKind kind, bool ok, const QString &failureDetail = {});
    void finalizeRuntimeRefreshIfComplete(quint64 generation);
    void refreshNow(RefreshOrigin origin);
    void startIpv4Probe(quint64 generation, RefreshOrigin origin);
    void startTimingProbe(quint64 generation);
    void startDnsProbe(quint64 generation);
    void rebuildGeoIpProvider();
    void applyGeoIpResolveResult(const GeoIpResolveResult &result);
    void readLocationFromPublicIp(RefreshOrigin origin);
    void emitSnapshotUpdate();

    config::AppConfig m_config;
    clash::ClashApiClient *m_client = nullptr;
    logging::LoggingService *m_logger = nullptr;
    QTimer m_timer;
    DiagnosticsSnapshot m_snapshot;
    QString m_lastObservedModeValue;
    quint64 m_probeGeneration = 0;
    RuntimeRefreshProgress m_runtimeRefreshProgress;
    QPointer<QProcess> m_ipv4Process;
    QPointer<QProcess> m_timingProcess;
    QPointer<QProcess> m_dnsProcess;
    QNetworkAccessManager m_networkManager;
    std::unique_ptr<GeoIpProvider> m_geoIpProvider;
};

}  // namespace tunlet::diagnostics
