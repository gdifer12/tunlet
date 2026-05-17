#include "diagnostics/diagnostics_parsing.hpp"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

namespace tunlet::diagnostics {
namespace {

bool isUsableIpAddress(const QString &value) {
    if (value.isEmpty() || value.startsWith("error:", Qt::CaseInsensitive) || value == "timeout") {
        return false;
    }

    QHostAddress address;
    return address.setAddress(value.trimmed());
}

QString readJsonStringByPath(const QJsonObject &object, const QString &path) {
    const QStringList parts = path.split('.');
    QJsonValue current = object;
    for (const QString &part : parts) {
        if (!current.isObject()) {
            return {};
        }
        current = current.toObject().value(part);
    }
    return current.isString() ? current.toString().trimmed() : QString{};
}

QString firstJsonString(const QJsonObject &object, std::initializer_list<const char *> paths) {
    for (const char *path : paths) {
        const QString value = readJsonStringByPath(object, QString::fromUtf8(path));
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString cleanIpToken(QString token) {
    token = token.trimmed();
    while (!token.isEmpty() && QString("[]()<>\"'").contains(token.front())) {
        token.remove(0, 1);
    }
    while (!token.isEmpty() && QString("[]()<>\"'.,;").contains(token.back())) {
        token.chop(1);
    }
    return token.trimmed();
}

QString extractUsableIpToken(const QString &text) {
    const QStringList tokens = text.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        const QString candidate = cleanIpToken(token);
        if (isUsableIpAddress(candidate)) {
            return candidate;
        }
    }
    return {};
}

qint64 secondsToMs(const QString &secondsText, bool *ok = nullptr) {
    bool conversionOk = false;
    const double seconds = secondsText.toDouble(&conversionOk);
    if (ok) {
        *ok = conversionOk;
    }
    return conversionOk ? qRound64(seconds * 1000.0) : -1;
}

}  // namespace

QString parsePublicIpOutput(const QByteArray &output, QString *failureReason) {
    const QString text = QString::fromUtf8(output).trimmed();
    if (text.isEmpty()) {
        if (failureReason) {
            *failureReason = "empty IP response";
        }
        return {};
    }

    const QString directIp = extractUsableIpToken(text);
    if (!directIp.isEmpty()) {
        return directIp;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error == QJsonParseError::NoError) {
        if (json.isObject()) {
            const QJsonObject object = json.object();
            for (const QString &value : {firstJsonString(object, {"ip", "query", "address", "ip_addr", "data.ip", "result.ip"}),
                                         firstJsonString(object, {"origin"})}) {
                const QString candidate = extractUsableIpToken(value);
                if (!candidate.isEmpty()) {
                    return candidate;
                }
            }
        } else if (json.isArray()) {
            const QJsonArray array = json.array();
            for (const QJsonValue &value : array) {
                if (!value.isString()) {
                    continue;
                }
                const QString candidate = extractUsableIpToken(value.toString());
                if (!candidate.isEmpty()) {
                    return candidate;
                }
            }
        }
    }

    if (failureReason) {
        *failureReason = "invalid IP response";
    }
    return {};
}

ParsedTimingResult parseTimingOutput(const QByteArray &output, QString *failureReason) {
    ParsedTimingResult result;
    const QString text = QString::fromUtf8(output).trimmed();
    if (text.isEmpty()) {
        if (failureReason) {
            *failureReason = "empty timing response";
        }
        return result;
    }

    const QRegularExpression tokenPattern(R"((dns|connect|tls|total)=([0-9]*\.?[0-9]+)s)");
    auto matchIterator = tokenPattern.globalMatch(text);
    while (matchIterator.hasNext()) {
        const auto match = matchIterator.next();
        bool ok = false;
        const qint64 valueMs = secondsToMs(match.captured(2), &ok);
        if (!ok) {
            continue;
        }

        const QString key = match.captured(1);
        if (key == "dns") {
            result.dnsMs = valueMs;
        } else if (key == "connect") {
            result.connectMs = valueMs;
        } else if (key == "tls") {
            result.tlsMs = valueMs;
        } else if (key == "total") {
            result.totalMs = valueMs;
        }
    }

    if (result.totalMs < 0) {
        if (failureReason) {
            *failureReason = "timing response did not include total=";
        }
        return result;
    }

    result.ok = true;
    return result;
}

QString parseDnsOutput(const QByteArray &output, QString *failureReason) {
    const QString text = QString::fromUtf8(output).trimmed();
    if (text.isEmpty()) {
        if (failureReason) {
            *failureReason = "empty DNS response";
        }
        return {};
    }

    QStringList cleanedLines;
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        line.replace('"', "");
        line = line.simplified();
        if (!line.isEmpty()) {
            cleanedLines.push_back(line);
        }
    }

    if (cleanedLines.isEmpty()) {
        if (failureReason) {
            *failureReason = "DNS response did not contain usable TXT data";
        }
        return {};
    }

    return cleanedLines.join(" | ");
}

ParsedGeoIpApiResponse parseIpWhoisResponse(const QByteArray &output, const QString &publicIp, QString *failureReason) {
    ParsedGeoIpApiResponse result;

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        if (failureReason) {
            *failureReason = parseError.error == QJsonParseError::NoError ? "GeoIP response was not a JSON object"
                                                                          : parseError.errorString();
        }
        return result;
    }

    const QJsonObject object = json.object();
    if (object.value("success").isBool() && !object.value("success").toBool()) {
        const QString providerMessage = object.value("message").toString().trimmed();
        if (failureReason) {
            *failureReason = providerMessage.isEmpty() ? "GeoIP provider reported success=false" : providerMessage;
        }
        return result;
    }

    GeoLocationRecord record;
    record.publicIp = publicIp;
    record.country = firstJsonString(object, {"country"});
    record.countryCode = firstJsonString(object, {"country_code"});
    record.region = firstJsonString(object, {"region"});
    record.city = firstJsonString(object, {"city"});
    record.timezone = firstJsonString(object, {"timezone.id", "timezone"});
    record.org = firstJsonString(object, {"connection.org"});
    record.isp = firstJsonString(object, {"connection.isp"});

    const QJsonValue asnValue = object.value("connection").toObject().value("asn");
    if (asnValue.isDouble()) {
        record.asn = QString::number(asnValue.toInteger());
    } else if (asnValue.isString()) {
        record.asn = asnValue.toString().trimmed();
    }

    const QJsonValue latitudeValue = object.value("latitude");
    const QJsonValue longitudeValue = object.value("longitude");
    if (latitudeValue.isDouble() && longitudeValue.isDouble()) {
        record.latitude = latitudeValue.toDouble();
        record.longitude = longitudeValue.toDouble();
        record.hasCoordinates = true;
    }

    if (record.country.isEmpty() && record.region.isEmpty() && record.city.isEmpty()) {
        if (failureReason) {
            *failureReason = "GeoIP response did not contain usable location fields";
        }
        return result;
    }

    result.ok = true;
    result.record = record;
    result.raw = object;
    return result;
}

}  // namespace tunlet::diagnostics
