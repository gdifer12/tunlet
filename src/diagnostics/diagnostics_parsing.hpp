#pragma once

#include "diagnostics/geoip_provider.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace tunlet::diagnostics {

struct ParsedTimingResult {
    bool ok = false;
    qint64 dnsMs = -1;
    qint64 connectMs = -1;
    qint64 tlsMs = -1;
    qint64 totalMs = -1;
};

QString parsePublicIpOutput(const QByteArray &output, QString *failureReason = nullptr);
ParsedTimingResult parseTimingOutput(const QByteArray &output, QString *failureReason = nullptr);
QString parseDnsOutput(const QByteArray &output, QString *failureReason = nullptr);

struct ParsedGeoIpApiResponse {
    bool ok = false;
    GeoLocationRecord record;
    QJsonObject raw;
};

ParsedGeoIpApiResponse parseIpWhoisResponse(const QByteArray &output, const QString &publicIp, QString *failureReason = nullptr);

}  // namespace tunlet::diagnostics
