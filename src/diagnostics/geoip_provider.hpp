#pragma once

#include "config/app_config.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace tunlet::diagnostics {

struct GeoLocationRecord {
    QString publicIp;
    QString country;
    QString countryCode;
    QString region;
    QString city;
    double latitude = 0.0;
    double longitude = 0.0;
    bool hasCoordinates = false;
    QString timezone;
    int accuracyRadius = -1;
    QString asn;
    QString org;
    QString isp;
    QString source;
    QString provider;
    QString providerUrl;
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime nextRefreshAt;
    bool stale = false;
};

enum class GeoIpResolveState {
    Disabled,
    Ready,
    Pending,
    Unavailable,
};

struct GeoIpResolveResult {
    GeoIpResolveState state = GeoIpResolveState::Unavailable;
    GeoLocationRecord record;
    QString detail;
};

using GeoIpResolveCallback = std::function<void(const GeoIpResolveResult &)>;

class GeoIpProvider : public QObject {
public:
    explicit GeoIpProvider(QObject *parent = nullptr) : QObject(parent) {}
    ~GeoIpProvider() override = default;

    virtual void updateConfig(const config::DiagnosticsLocationConfig &config) = 0;
    virtual GeoIpResolveResult readLocation(const QString &publicIp, GeoIpResolveCallback callback) = 0;
    virtual GeoIpResolveResult bootstrapLocationOnMiss(const QString &publicIp, GeoIpResolveCallback callback) = 0;
    virtual GeoIpResolveResult refreshLocationData(const QString &publicIp, GeoIpResolveCallback callback) = 0;
    virtual void cancelPending() = 0;
};

std::unique_ptr<GeoIpProvider> createGeoIpProvider(const config::DiagnosticsLocationConfig &config,
                                                   QNetworkAccessManager *networkManager,
                                                   QObject *parent = nullptr);
QString formatGeoLocationSummary(const GeoLocationRecord &record);
QString formatGeoLocationAsnOrg(const GeoLocationRecord &record);
QString formatGeoLocationSource(const GeoLocationRecord &record);

}  // namespace tunlet::diagnostics
