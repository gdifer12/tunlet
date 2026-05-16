#pragma once

#include "config/app_config.hpp"

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>

namespace tunlet::clash {

QString toClashWriteMode(const QString &targetMode);

struct HealthCheckResult {
    bool ok = false;
    QString detail;
    int httpStatus = 0;
    qint64 latencyMs = -1;
};

struct TrafficResult {
    bool ok = false;
    QString detail;
    double uploadKbps = 0.0;
    double downloadKbps = 0.0;
    double uploadTotalBytes = -1.0;
    double downloadTotalBytes = -1.0;
    int httpStatus = 0;
};

struct ModeStateResult {
    bool ok = false;
    QString detail;
    QString currentMode;
    QStringList supportedModes;
    int httpStatus = 0;
};

struct ModeSwitchResult {
    bool ok = false;
    QString detail;
    QString targetMode;
    int httpStatus = 0;
};

class ClashApiClient : public QObject {
    Q_OBJECT

public:
    explicit ClashApiClient(int timeoutMs, QObject *parent = nullptr);

    void setTimeoutMs(int timeoutMs);
    void checkHealth(const config::ClashApiConfig &apiConfig);
    void fetchTraffic(const config::ClashApiConfig &apiConfig);
    void fetchCurrentMode(const config::ClashApiConfig &apiConfig);
    void switchMode(const config::ClashApiConfig &apiConfig, const QString &targetMode);

signals:
    void healthCheckFinished(const tunlet::clash::HealthCheckResult &result);
    void trafficFinished(const tunlet::clash::TrafficResult &result);
    void modeStateFinished(const tunlet::clash::ModeStateResult &result);
    void modeSwitchFinished(const tunlet::clash::ModeSwitchResult &result);

private:
    QUrl buildUrl(const config::ClashApiConfig &apiConfig, const QString &path) const;
    void attachTimeout(QNetworkReply *reply) const;
    QString describeNetworkReply(QNetworkReply *reply) const;

    QNetworkAccessManager m_network;
    int m_timeoutMs = 5000;
};

}  // namespace tunlet::clash
