#pragma once

#include "clash/clash_api_client.hpp"
#include "config/app_config.hpp"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>

namespace tunlet::clash {
struct ModeStatus;
}

namespace tunlet::diagnostics {

struct DiagnosticsSnapshot {
    bool apiReachable = false;
    QString apiDetail = "No diagnostics yet";
    QString ipv4;
    QString ipv6;
    QString proxyIp;
    QString location;
    QString locationDetail = "Location unavailable";
    qint64 apiLatencyMs = -1;
    bool trafficAvailable = false;
    QString trafficSummary = "Unavailable";
    QString trafficDetail = "Traffic metrics unavailable";
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
    void issueOptionalExternalRequest(const QString &url, const char *fieldName);
    void issueLocationLookup(const QString &ipAddress);
    void issueLocationLookupRequest(const QString &ipAddress, const QStringList &urls, int index);
    void handleHealthResult(const clash::HealthCheckResult &result);
    void updateExternalField(const QString &fieldName, const QString &value);

    config::AppConfig m_config;
    clash::ClashApiClient *m_client = nullptr;
    QNetworkAccessManager m_network;
    QTimer m_timer;
    DiagnosticsSnapshot m_snapshot;
    QString m_lastLocationLookupIp;
    QString m_lastObservedModeValue;
};

}  // namespace tunlet::diagnostics
