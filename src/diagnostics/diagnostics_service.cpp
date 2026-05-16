#include "diagnostics/diagnostics_service.hpp"

#include "clash/mode_controller.hpp"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace tunlet::diagnostics {
namespace {

bool isUsableIpAddress(const QString &value) {
    if (value.isEmpty() || value.startsWith("error:", Qt::CaseInsensitive) || value == "timeout") {
        return false;
    }

    QHostAddress address;
    return address.setAddress(value.trimmed());
}

QString formatRate(double valueKbps) {
    if (valueKbps < 0.0) {
        return "-";
    }
    if (valueKbps >= 1000.0) {
        return QString("%1 Mbps").arg(valueKbps / 1000.0, 0, 'f', 1);
    }
    return QString("%1 kbps").arg(valueKbps, 0, 'f', valueKbps >= 100.0 ? 0 : 1);
}

QString formatBytes(double bytes) {
    if (bytes < 0.0) {
        return "n/a";
    }

    static const char *suffixes[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int suffixIndex = 0;
    double value = bytes;
    while (value >= 1024.0 && suffixIndex < 4) {
        value /= 1024.0;
        ++suffixIndex;
    }

    const int precision = value >= 100.0 || suffixIndex == 0 ? 0 : 1;
    return QString("%1 %2").arg(value, 0, 'f', precision).arg(QString::fromUtf8(suffixes[suffixIndex]));
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

QString parseExternalIpResponse(const QByteArray &body) {
    const QString text = QString::fromUtf8(body).trimmed();
    if (text.isEmpty()) {
        return "error: empty response";
    }

    const QString directIp = extractUsableIpToken(text);
    if (!directIp.isEmpty()) {
        return directIp;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(body, &parseError);
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

    return "error: invalid IP response";
}

QString normalizeExternalRequestUrl(const QString &urlText, const QString &fieldName) {
    QUrl url(urlText);
    if (!url.isValid()) {
        return urlText;
    }

    const QString host = url.host().toLower();
    const QString path = url.path();
    if (fieldName == "proxy" && host == "ifconfig.me" && (path.isEmpty() || path == "/")) {
        url.setPath("/ip");
    }

    return url.toString();
}

QString applyIpTemplate(QString templateUrl, const QString &ipAddress) {
    templateUrl.replace("{ip}", QString::fromUtf8(QUrl::toPercentEncoding(ipAddress)));
    return templateUrl;
}

QStringList locationLookupUrls(const config::ExternalIpConfig &externalIp, const QString &ipAddress) {
    QStringList urls;
    if (!externalIp.locationUrlTemplate.trimmed().isEmpty()) {
        urls.push_back(applyIpTemplate(externalIp.locationUrlTemplate, ipAddress));
    }

    urls.push_back(applyIpTemplate("https://ipapi.co/{ip}/json/", ipAddress));
    urls.push_back(applyIpTemplate("https://ipinfo.io/{ip}/json", ipAddress));
    urls.removeDuplicates();
    return urls;
}

bool tryApplyLocationResponse(DiagnosticsSnapshot &snapshot, const QString &ipAddress, const QByteArray &body, QString *failureReason) {
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        if (failureReason) {
            *failureReason = QString("invalid location JSON: %1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject object = json.object();
    const bool explicitFailure =
        (object.contains("success") && object.value("success").isBool() && !object.value("success").toBool()) ||
        object.value("status").toString() == "fail" ||
        object.contains("error");
    if (explicitFailure) {
        const QString message = firstJsonString(object, {"message", "reason", "error"});
        if (failureReason) {
            *failureReason = message.isEmpty() ? "location service rejected the lookup" : message;
        }
        return false;
    }

    const QString city = firstJsonString(object, {"city"});
    const QString region = firstJsonString(object, {"region", "regionName"});
    const QString country = firstJsonString(object, {"country", "country_name"});
    const QString isp = firstJsonString(object, {"isp", "org", "organization", "connection.isp"});

    QStringList parts;
    if (!city.isEmpty()) {
        parts.push_back(city);
    }
    if (!region.isEmpty() && region != city) {
        parts.push_back(region);
    }
    if (!country.isEmpty()) {
        parts.push_back(country);
    }

    snapshot.location = parts.isEmpty() ? "Unknown location" : parts.join(", ");
    snapshot.locationDetail =
        isp.isEmpty() ? QString("Lookup IP: %1").arg(ipAddress)
                      : QString("Lookup IP: %1 | ISP: %2").arg(ipAddress, isp);
    return true;
}

}  // namespace

DiagnosticsService::DiagnosticsService(const config::AppConfig &config, clash::ClashApiClient *client, QObject *parent)
    : QObject(parent), m_config(config), m_client(client) {
    connect(m_client, &clash::ClashApiClient::healthCheckFinished, this, &DiagnosticsService::handleHealthResult);
    connect(m_client, &clash::ClashApiClient::trafficFinished, this, &DiagnosticsService::handleTrafficResult);
    connect(&m_timer, &QTimer::timeout, this, &DiagnosticsService::refreshNow);
}

void DiagnosticsService::start() {
    if (!m_config.diagnostics.enabled) {
        return;
    }

    m_timer.start(m_config.diagnostics.refreshIntervalMs);
    refreshNow();
}

void DiagnosticsService::refreshNow() {
    if (!m_config.diagnostics.enabled) {
        return;
    }

    m_client->checkHealth(m_config.clashApi);
    m_client->fetchTraffic(m_config.clashApi);

    if (!m_config.diagnostics.externalIp.enabled) {
        return;
    }

    if (!m_config.diagnostics.externalIp.ipv4Url.isEmpty()) {
        issueOptionalExternalRequest(m_config.diagnostics.externalIp.ipv4Url, "ipv4");
    }
    if (!m_config.diagnostics.externalIp.ipv6Url.isEmpty()) {
        issueOptionalExternalRequest(m_config.diagnostics.externalIp.ipv6Url, "ipv6");
    }
    if (!m_config.diagnostics.externalIp.proxyUrl.isEmpty()) {
        issueOptionalExternalRequest(m_config.diagnostics.externalIp.proxyUrl, "proxy");
    }
}

void DiagnosticsService::updateConfig(const config::AppConfig &config) {
    const bool wasEnabled = m_config.diagnostics.enabled;
    m_config = config;
    m_lastObservedModeValue.clear();
    if (!m_config.diagnostics.enabled) {
        m_timer.stop();
        return;
    }

    m_timer.start(m_config.diagnostics.refreshIntervalMs);
    if (!wasEnabled) {
        refreshNow();
    }
}

DiagnosticsSnapshot DiagnosticsService::snapshot() const {
    return m_snapshot;
}

void DiagnosticsService::observeModeStatus(const tunlet::clash::ModeStatus &status) {
    if (status.busy || status.currentModeValue.isEmpty()) {
        return;
    }

    if (m_lastObservedModeValue.isEmpty()) {
        m_lastObservedModeValue = status.currentModeValue;
        return;
    }

    if (QString::compare(m_lastObservedModeValue, status.currentModeValue, Qt::CaseInsensitive) == 0) {
        return;
    }

    m_lastObservedModeValue = status.currentModeValue;
    refreshNow();
}

void DiagnosticsService::issueOptionalExternalRequest(const QString &url, const char *fieldName) {
    const QString field = QString::fromUtf8(fieldName);
    auto *reply = m_network.get(QNetworkRequest(QUrl(normalizeExternalRequestUrl(url, field))));
    QTimer::singleShot(m_config.diagnostics.requestTimeoutMs, reply, [reply]() {
        if (reply->isRunning()) {
            reply->setProperty("timedOut", true);
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, fieldName]() {
        const QString field = QString::fromUtf8(fieldName);
        QString value;
        if (reply->property("timedOut").toBool()) {
            value = "timeout";
        } else if (reply->error() != QNetworkReply::NoError) {
            value = QString("error: %1").arg(reply->errorString());
        } else {
            value = parseExternalIpResponse(reply->readAll());
        }

        updateExternalField(field, value);
        reply->deleteLater();
    });
}

void DiagnosticsService::handleHealthResult(const clash::HealthCheckResult &result) {
    m_snapshot.apiReachable = result.ok;
    m_snapshot.apiDetail = result.detail;
    m_snapshot.apiLatencyMs = result.latencyMs;
    m_snapshot.lastUpdated = QDateTime::currentDateTime();
    emit diagnosticsUpdated(m_snapshot);
}

void DiagnosticsService::handleTrafficResult(const clash::TrafficResult &result) {
    m_snapshot.trafficAvailable = result.ok;
    if (!result.ok) {
        m_snapshot.trafficSummary = "Unavailable";
        m_snapshot.trafficDetail = result.detail;
    } else {
        m_snapshot.trafficSummary =
            QString("Down %1 | Up %2").arg(formatRate(result.downloadKbps), formatRate(result.uploadKbps));
        if (result.downloadTotalBytes >= 0.0 || result.uploadTotalBytes >= 0.0) {
            m_snapshot.trafficDetail =
                QString("Total down %1 | Total up %2")
                    .arg(formatBytes(result.downloadTotalBytes), formatBytes(result.uploadTotalBytes));
        } else {
            m_snapshot.trafficDetail = "Kernel reports current throughput only.";
        }
    }

    m_snapshot.lastUpdated = QDateTime::currentDateTime();
    emit diagnosticsUpdated(m_snapshot);
}

void DiagnosticsService::updateExternalField(const QString &fieldName, const QString &value) {
    if (fieldName == "ipv4") {
        m_snapshot.ipv4 = value;
    } else if (fieldName == "ipv6") {
        m_snapshot.ipv6 = value;
    } else if (fieldName == "proxy") {
        m_snapshot.proxyIp = value;
    }

    m_snapshot.externalDetail = "External diagnostics updated";
    const QString locationCandidate = isUsableIpAddress(m_snapshot.proxyIp)
                                          ? m_snapshot.proxyIp
                                          : (isUsableIpAddress(m_snapshot.ipv4) ? m_snapshot.ipv4 : m_snapshot.ipv6);
    if (!locationCandidate.isEmpty()) {
        issueLocationLookup(locationCandidate);
    } else {
        m_snapshot.location.clear();
        m_snapshot.locationDetail = "Waiting for a usable public IP before resolving location.";
        m_lastLocationLookupIp.clear();
    }
    m_snapshot.lastUpdated = QDateTime::currentDateTime();
    emit diagnosticsUpdated(m_snapshot);
}

void DiagnosticsService::issueLocationLookup(const QString &ipAddress) {
    if (ipAddress.isEmpty() || (ipAddress == m_lastLocationLookupIp && !m_snapshot.location.isEmpty())) {
        return;
    }

    m_lastLocationLookupIp = ipAddress;
    m_snapshot.location = "Resolving...";
    m_snapshot.locationDetail = QString("Resolving location for %1...").arg(ipAddress);
    m_snapshot.lastUpdated = QDateTime::currentDateTime();
    emit diagnosticsUpdated(m_snapshot);

    const QStringList urls = locationLookupUrls(m_config.diagnostics.externalIp, ipAddress);
    issueLocationLookupRequest(ipAddress, urls, 0);
}

void DiagnosticsService::issueLocationLookupRequest(const QString &ipAddress, const QStringList &urls, int index) {
    if (index >= urls.size()) {
        m_snapshot.location.clear();
        if (m_snapshot.locationDetail.startsWith("Resolving location for ")) {
            m_snapshot.locationDetail = QString("Location lookup failed for %1.").arg(ipAddress);
        }
        m_snapshot.lastUpdated = QDateTime::currentDateTime();
        emit diagnosticsUpdated(m_snapshot);
        return;
    }

    auto *reply = m_network.get(QNetworkRequest(QUrl(urls.at(index))));
    QTimer::singleShot(m_config.diagnostics.requestTimeoutMs, reply, [reply]() {
        if (reply->isRunning()) {
            reply->setProperty("timedOut", true);
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, ipAddress, urls, index]() {
        QString failureReason;
        if (reply->property("timedOut").toBool()) {
            failureReason = QString("timed out via %1").arg(QUrl(urls.at(index)).host());
        } else if (reply->error() != QNetworkReply::NoError) {
            failureReason = QString("%1 via %2").arg(reply->errorString(), QUrl(urls.at(index)).host());
        } else {
            const QByteArray body = reply->readAll();
            if (!tryApplyLocationResponse(m_snapshot, ipAddress, body, &failureReason)) {
                m_snapshot.location.clear();
            }
        }

        const bool success = !m_snapshot.location.isEmpty();
        if (!success && index + 1 < urls.size()) {
            reply->deleteLater();
            issueLocationLookupRequest(ipAddress, urls, index + 1);
            return;
        }

        if (!success) {
            m_snapshot.locationDetail = QString("Location lookup failed for %1: %2").arg(ipAddress, failureReason);
        }
        m_snapshot.lastUpdated = QDateTime::currentDateTime();
        emit diagnosticsUpdated(m_snapshot);
        reply->deleteLater();
    });
}

}  // namespace tunlet::diagnostics
