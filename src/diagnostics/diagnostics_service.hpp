#pragma once

#include "clash/clash_api_client.hpp"
#include "config/app_config.hpp"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

class QNetworkReply;

namespace tunlet::clash {
struct ModeStatus;
}

namespace tunlet::diagnostics {

struct DiagnosticsSnapshot {
    bool apiReachable = false;
    QString apiDetail = "No diagnostics yet";
    QString publicIp;
    QString publicIpDetail = "Public IP unavailable";
    QString location;
    QString locationDetail = "Location unavailable";
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
    QDateTime lastUpdated;
};

class DiagnosticsService : public QObject {
    Q_OBJECT

public:
    DiagnosticsService(const config::AppConfig &config, clash::ClashApiClient *client, QObject *parent = nullptr);

    void start();
    void refreshNow();
    void updateConfig(const config::AppConfig &config);
    void observeModeStatus(const tunlet::clash::ModeStatus &status);
    DiagnosticsSnapshot snapshot() const;

signals:
    void diagnosticsUpdated(const tunlet::diagnostics::DiagnosticsSnapshot &snapshot);

private:
    void handleTrafficResult(const clash::TrafficResult &result);
    void handleHealthResult(const clash::HealthCheckResult &result);
    void updateConfigurationSnapshot();
    void resetConnectionSnapshot(const QString &reason);
    void abortProbe(QPointer<QProcess> &process);
    void startIpv4Probe(quint64 generation);
    void startTimingProbe(quint64 generation);
    void startDnsProbe(quint64 generation);
    void abortLocationDownload();
    bool ensureLocationDatabaseAvailable(const QString &dbPath);
    void startLocationDatabaseDownload(const QString &dbPath, const QString &downloadUrl);
    void updateLocationFromPublicIp();
    void emitSnapshotUpdate();

    config::AppConfig m_config;
    clash::ClashApiClient *m_client = nullptr;
    QTimer m_timer;
    DiagnosticsSnapshot m_snapshot;
    QString m_lastObservedModeValue;
    quint64 m_probeGeneration = 0;
    QPointer<QProcess> m_ipv4Process;
    QPointer<QProcess> m_timingProcess;
    QPointer<QProcess> m_dnsProcess;
    QNetworkAccessManager m_networkManager;
    QPointer<QNetworkReply> m_geoDbReply;
};

}  // namespace tunlet::diagnostics
