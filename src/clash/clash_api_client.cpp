#include "clash/clash_api_client.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDateTime>
#include <QTimer>
#include <QUrl>

namespace tunlet::clash {
namespace {

bool isSuccessfulHttpStatus(int httpStatus) {
    return httpStatus >= 200 && httpStatus < 300;
}

QString readReplyBody(QNetworkReply *reply) {
    return QString::fromUtf8(reply->readAll()).trimmed();
}

double readFirstNumericField(const QJsonObject &object, std::initializer_list<const char *> keys) {
    for (const char *key : keys) {
        const QJsonValue value = object.value(QString::fromUtf8(key));
        if (value.isDouble()) {
            return value.toDouble();
        }
    }
    return -1.0;
}

}  // namespace

QString toClashWriteMode(const QString &targetMode) {
    const QString normalized = targetMode.trimmed().toLower();
    if (normalized == "direct") {
        return "Direct";
    }
    if (normalized == "global") {
        return "Global";
    }
    if (normalized == "rule") {
        return "Rule";
    }
    return targetMode.trimmed();
}

ClashApiClient::ClashApiClient(int timeoutMs, QObject *parent)
    : QObject(parent), m_timeoutMs(timeoutMs) {}

void ClashApiClient::setTimeoutMs(int timeoutMs) {
    m_timeoutMs = timeoutMs;
}

QUrl ClashApiClient::buildUrl(const config::ClashApiConfig &apiConfig, const QString &path) const {
    QUrl url;
    url.setScheme("http");
    url.setHost(apiConfig.host);
    url.setPort(apiConfig.port);
    url.setPath(path);
    return url;
}

void ClashApiClient::attachTimeout(QNetworkReply *reply) const {
    QTimer::singleShot(m_timeoutMs, reply, [reply]() {
        if (reply->isRunning()) {
            reply->setProperty("timedOut", true);
            reply->abort();
        }
    });
}

QString ClashApiClient::describeNetworkReply(QNetworkReply *reply) const {
    if (reply->property("timedOut").toBool()) {
        return QString("request timed out after %1 ms").arg(m_timeoutMs);
    }

    if (reply->error() != QNetworkReply::NoError) {
        return reply->errorString();
    }

    return {};
}

void ClashApiClient::checkHealth(const config::ClashApiConfig &apiConfig) {
    const qint64 startedAtMs = QDateTime::currentMSecsSinceEpoch();
    QNetworkRequest request(buildUrl(apiConfig, "/version"));
    auto *reply = m_network.get(request);
    attachTimeout(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, startedAtMs]() {
        HealthCheckResult result;
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.latencyMs = QDateTime::currentMSecsSinceEpoch() - startedAtMs;

        const QString networkError = describeNetworkReply(reply);
        if (!networkError.isEmpty()) {
            result.detail = networkError;
        } else if (!isSuccessfulHttpStatus(result.httpStatus)) {
            const QString body = readReplyBody(reply);
            result.detail = body.isEmpty()
                ? QString("API health check failed with HTTP %1").arg(result.httpStatus)
                : QString("API health check failed with HTTP %1: %2").arg(result.httpStatus).arg(body);
        } else {
            result.ok = true;
            result.detail = "API reachable";
        }

        reply->deleteLater();
        emit healthCheckFinished(result);
    });
}

void ClashApiClient::fetchTraffic(const config::ClashApiConfig &apiConfig) {
    QNetworkRequest request(buildUrl(apiConfig, "/traffic"));
    auto *reply = m_network.get(request);
    attachTimeout(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        TrafficResult result;
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        const QString networkError = describeNetworkReply(reply);
        if (!networkError.isEmpty()) {
            result.detail = networkError;
            reply->deleteLater();
            emit trafficFinished(result);
            return;
        }

        if (!isSuccessfulHttpStatus(result.httpStatus)) {
            const QString body = readReplyBody(reply);
            result.detail = body.isEmpty()
                ? QString("failed to load traffic: HTTP %1").arg(result.httpStatus)
                : QString("failed to load traffic: HTTP %1: %2").arg(result.httpStatus).arg(body);
            reply->deleteLater();
            emit trafficFinished(result);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument json = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
            result.detail = QString("invalid traffic JSON: %1").arg(parseError.errorString());
            reply->deleteLater();
            emit trafficFinished(result);
            return;
        }

        const QJsonObject object = json.object();
        result.uploadKbps = readFirstNumericField(object, {"up", "upload"});
        result.downloadKbps = readFirstNumericField(object, {"down", "download"});
        result.uploadTotalBytes = readFirstNumericField(object, {"upTotal", "uploadTotal"});
        result.downloadTotalBytes = readFirstNumericField(object, {"downTotal", "downloadTotal"});

        if (result.uploadKbps < 0.0 || result.downloadKbps < 0.0) {
            result.detail = "traffic response did not include upload/download values";
            reply->deleteLater();
            emit trafficFinished(result);
            return;
        }

        result.ok = true;
        result.detail = "traffic metrics loaded";
        reply->deleteLater();
        emit trafficFinished(result);
    });
}

void ClashApiClient::fetchCurrentMode(const config::ClashApiConfig &apiConfig) {
    QNetworkRequest request(buildUrl(apiConfig, "/configs"));
    auto *reply = m_network.get(request);
    attachTimeout(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        ModeStateResult result;
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        const QString networkError = describeNetworkReply(reply);
        if (!networkError.isEmpty()) {
            result.detail = networkError;
            reply->deleteLater();
            emit modeStateFinished(result);
            return;
        }

        if (!isSuccessfulHttpStatus(result.httpStatus)) {
            const QString body = readReplyBody(reply);
            result.detail = body.isEmpty()
                ? QString("failed to load configs: HTTP %1").arg(result.httpStatus)
                : QString("failed to load configs: HTTP %1: %2").arg(result.httpStatus).arg(body);
            reply->deleteLater();
            emit modeStateFinished(result);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument json = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
            result.detail = QString("invalid API JSON: %1").arg(parseError.errorString());
            reply->deleteLater();
            emit modeStateFinished(result);
            return;
        }

        const QJsonObject object = json.object();
        result.currentMode = object.value("mode").toString();
        const QJsonArray modeList = object.value("mode-list").toArray();
        for (const auto &item : modeList) {
            result.supportedModes.push_back(item.toString());
        }

        if (result.currentMode.isEmpty()) {
            result.detail = "configs response did not include current mode";
        } else {
            result.ok = true;
            result.detail = "current mode loaded";
        }

        reply->deleteLater();
        emit modeStateFinished(result);
    });
}

void ClashApiClient::switchMode(const config::ClashApiConfig &apiConfig, const QString &targetMode) {
    QNetworkRequest request(buildUrl(apiConfig, "/configs"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");

    QJsonObject payload;
    const QString requestMode = toClashWriteMode(targetMode);
    payload.insert("mode", requestMode);

    auto *reply = m_network.sendCustomRequest(
        request,
        "PATCH",
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    attachTimeout(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, targetMode, requestMode]() {
        ModeSwitchResult result;
        result.targetMode = targetMode;
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        const QString networkError = describeNetworkReply(reply);
        if (!networkError.isEmpty()) {
            result.detail = networkError;
        } else if (!isSuccessfulHttpStatus(result.httpStatus)) {
            const QString body = readReplyBody(reply);
            result.detail = body.isEmpty()
                ? QString("failed to switch mode via PATCH /configs: HTTP %1").arg(result.httpStatus)
                : QString("failed to switch mode via PATCH /configs: HTTP %1: %2").arg(result.httpStatus).arg(body);
        } else {
            result.ok = true;
            result.detail = QString("requested mode '%1' via Clash value '%2'").arg(targetMode, requestMode);
        }

        reply->deleteLater();
        emit modeSwitchFinished(result);
    });
}

}  // namespace tunlet::clash
