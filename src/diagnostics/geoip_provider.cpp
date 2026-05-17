#include "diagnostics/geoip_provider.hpp"

#include "diagnostics/diagnostics_parsing.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <maxminddb.h>

#include <cmath>

namespace tunlet::diagnostics {
namespace {

constexpr int kGeoIpCacheSchemaVersion = 1;

QString formatDate(const QDateTime &value) {
    return value.isValid() ? value.toUTC().toString(Qt::ISODate) : QString();
}

QDateTime parseDate(const QJsonValue &value) {
    if (!value.isString()) {
        return {};
    }
    return QDateTime::fromString(value.toString(), Qt::ISODate);
}

bool isUsableIpAddress(const QString &value) {
    if (value.isEmpty()) {
        return false;
    }

    QHostAddress address;
    return address.setAddress(value.trimmed());
}

QString readMmdbString(const MMDB_entry_s *entry,
                       const char *segment1,
                       const char *segment2 = nullptr,
                       const char *segment3 = nullptr,
                       const char *segment4 = nullptr) {
    MMDB_entry_data_s data;
    const int status =
        MMDB_get_value(const_cast<MMDB_entry_s *>(entry), &data, segment1, segment2, segment3, segment4, nullptr);
    if (status != MMDB_SUCCESS || !data.has_data || data.type != MMDB_DATA_TYPE_UTF8_STRING) {
        return {};
    }
    return QString::fromUtf8(data.utf8_string, data.data_size).trimmed();
}

int readMmdbInt(const MMDB_entry_s *entry,
                const char *segment1,
                const char *segment2 = nullptr,
                const char *segment3 = nullptr,
                const char *segment4 = nullptr) {
    MMDB_entry_data_s data;
    const int status =
        MMDB_get_value(const_cast<MMDB_entry_s *>(entry), &data, segment1, segment2, segment3, segment4, nullptr);
    if (status != MMDB_SUCCESS || !data.has_data) {
        return -1;
    }
    if (data.type == MMDB_DATA_TYPE_UINT16) {
        return static_cast<int>(data.uint16);
    }
    if (data.type == MMDB_DATA_TYPE_UINT32) {
        return static_cast<int>(data.uint32);
    }
    return -1;
}

QString cacheKeyFor(const QString &provider, const QString &publicIp) {
    return QString("%1:%2").arg(provider.trimmed().toLower(), publicIp.trimmed());
}

QString providerNameForSource(const QString &provider) {
    return provider.trimmed().isEmpty() ? QString("unknown") : provider.trimmed().toLower();
}

QString jitteredRefreshIsoKey(const QString &provider, const QString &publicIp, const QDateTime &updatedAt) {
    return QString("%1:%2:%3").arg(provider, publicIp, updatedAt.toUTC().toString(Qt::ISODate));
}

QDateTime computeNextRefreshAt(const config::DiagnosticsLocationDynamicCacheConfig &config,
                               const QString &provider,
                               const QString &publicIp,
                               const QDateTime &updatedAt) {
    const qint64 baseSecs = static_cast<qint64>(config.baseRefreshDays) * 24 * 60 * 60;
    const qint64 shiftSecs = static_cast<qint64>(config.randomShiftDays) * 24 * 60 * 60;
    qint64 jitterSecs = 0;
    if (shiftSecs > 0) {
        const QByteArray hash =
            QCryptographicHash::hash(jitteredRefreshIsoKey(provider, publicIp, updatedAt).toUtf8(), QCryptographicHash::Sha256);
        quint32 seed = 0;
        for (int index = 0; index < qMin(4, hash.size()); ++index) {
            seed = (seed << 8) | static_cast<unsigned char>(hash.at(index));
        }
        const qint64 range = shiftSecs * 2 + 1;
        jitterSecs = static_cast<qint64>(seed % range) - shiftSecs;
    }

    return updatedAt.addSecs(baseSecs + jitterSecs);
}

QJsonObject recordToJson(const GeoLocationRecord &record, const QJsonObject &raw) {
    QJsonObject normalized;
    normalized.insert("country", record.country);
    normalized.insert("country_code", record.countryCode);
    normalized.insert("region", record.region);
    normalized.insert("city", record.city);
    if (record.hasCoordinates) {
        normalized.insert("latitude", record.latitude);
        normalized.insert("longitude", record.longitude);
    }
    normalized.insert("timezone", record.timezone);
    if (record.accuracyRadius >= 0) {
        normalized.insert("accuracy_radius", record.accuracyRadius);
    }
    normalized.insert("asn", record.asn);
    normalized.insert("org", record.org);
    normalized.insert("isp", record.isp);

    QJsonObject object;
    object.insert("provider", record.provider);
    object.insert("provider_url", record.providerUrl);
    object.insert("public_ip", record.publicIp);
    object.insert("schema_version", kGeoIpCacheSchemaVersion);
    object.insert("created_at", formatDate(record.createdAt));
    object.insert("updated_at", formatDate(record.updatedAt));
    object.insert("next_refresh_at", formatDate(record.nextRefreshAt));
    object.insert("normalized", normalized);
    object.insert("raw", raw);
    return object;
}

bool recordFromJson(const QJsonObject &object,
                    GeoLocationRecord *record,
                    QJsonObject *raw,
                    QString *failureReason) {
    if (!record) {
        return false;
    }

    if (object.value("schema_version").toInt(kGeoIpCacheSchemaVersion) != kGeoIpCacheSchemaVersion) {
        if (failureReason) {
            *failureReason = "record schema version is incompatible";
        }
        return false;
    }

    const QJsonObject normalized = object.value("normalized").toObject();
    if (normalized.isEmpty()) {
        if (failureReason) {
            *failureReason = "record is missing normalized payload";
        }
        return false;
    }

    record->provider = object.value("provider").toString().trimmed();
    record->providerUrl = object.value("provider_url").toString().trimmed();
    record->publicIp = object.value("public_ip").toString().trimmed();
    record->createdAt = parseDate(object.value("created_at"));
    record->updatedAt = parseDate(object.value("updated_at"));
    record->nextRefreshAt = parseDate(object.value("next_refresh_at"));
    record->country = normalized.value("country").toString().trimmed();
    record->countryCode = normalized.value("country_code").toString().trimmed();
    record->region = normalized.value("region").toString().trimmed();
    record->city = normalized.value("city").toString().trimmed();
    record->timezone = normalized.value("timezone").toString().trimmed();
    record->asn = normalized.value("asn").toString().trimmed();
    record->org = normalized.value("org").toString().trimmed();
    record->isp = normalized.value("isp").toString().trimmed();

    if (normalized.value("latitude").isDouble() && normalized.value("longitude").isDouble()) {
        record->latitude = normalized.value("latitude").toDouble();
        record->longitude = normalized.value("longitude").toDouble();
        record->hasCoordinates = true;
    }
    if (normalized.value("accuracy_radius").isDouble()) {
        record->accuracyRadius = normalized.value("accuracy_radius").toInt();
    }

    if (record->provider.isEmpty() || record->publicIp.isEmpty()) {
        if (failureReason) {
            *failureReason = "record is missing provider or public_ip";
        }
        return false;
    }

    if (raw) {
        *raw = object.value("raw").toObject();
    }
    return true;
}

class DisabledGeoIpProvider final : public GeoIpProvider {
public:
    explicit DisabledGeoIpProvider(QObject *parent = nullptr) : GeoIpProvider(parent) {}

    void updateConfig(const config::DiagnosticsLocationConfig &) override {}

    GeoIpResolveResult readLocation(const QString &, GeoIpResolveCallback) override {
        return {.state = GeoIpResolveState::Disabled, .detail = "Location lookup disabled in config."};
    }

    GeoIpResolveResult refreshLocationData(const QString &, GeoIpResolveCallback) override {
        return {.state = GeoIpResolveState::Disabled, .detail = "Location lookup disabled in config."};
    }

    void cancelPending() override {}
};

class LocalMmdbGeoIpProvider final : public GeoIpProvider {
public:
    LocalMmdbGeoIpProvider(const config::DiagnosticsLocationConfig &config,
                           QNetworkAccessManager *networkManager,
                           QObject *parent = nullptr)
        : GeoIpProvider(parent), m_config(config), m_networkManager(networkManager) {}

    void updateConfig(const config::DiagnosticsLocationConfig &config) override {
        cancelPending();
        m_config = config;
    }

    GeoIpResolveResult readLocation(const QString &publicIp, GeoIpResolveCallback) override {
        if (!isUsableIpAddress(publicIp)) {
            return {.state = GeoIpResolveState::Unavailable, .detail = "Waiting for a usable public IP before resolving location."};
        }

        const QFileInfo dbInfo(m_config.localDb.databasePath);
        if (!dbInfo.exists() || !dbInfo.isFile()) {
            return {
                .state = GeoIpResolveState::Unavailable,
                .detail = QString("Location lookup failed for %1: Geo DB not found at %2")
                              .arg(publicIp, m_config.localDb.databasePath),
            };
        }

        GeoIpResolveResult result = lookup(publicIp);
        if (result.state == GeoIpResolveState::Ready) {
            result.record.source = "local DB";
        }
        return result;
    }

    GeoIpResolveResult refreshLocationData(const QString &publicIp, GeoIpResolveCallback callback) override {
        const QString downloadUrl = m_config.localDb.downloadUrl.trimmed();
        if (downloadUrl.isEmpty()) {
            return {.state = GeoIpResolveState::Unavailable, .detail = "Local DB update is not configured."};
        }

        if (m_downloadReply) {
            m_pendingPublicIp = publicIp;
            m_pendingCallback = callback;
            m_pendingDetail = "Local DB update already in progress.";
            return {.state = GeoIpResolveState::Pending, .detail = m_pendingDetail};
        }

        QUrl url(downloadUrl);
        if (!url.isValid() || url.scheme().trimmed().isEmpty()) {
            return {.state = GeoIpResolveState::Unavailable,
                    .detail = QString("Local DB update failed: invalid bootstrap URL %1").arg(downloadUrl)};
        }

        QFileInfo dbInfo(m_config.localDb.databasePath);
        QDir dbDir = dbInfo.dir();
        if (!dbDir.exists() && !dbDir.mkpath(".")) {
            return {.state = GeoIpResolveState::Unavailable,
                    .detail = QString("Local DB update failed: failed to create directory %1").arg(dbDir.absolutePath())};
        }

        return startLocationDatabaseDownload(publicIp, callback, /*forcedUpdate=*/true);
    }

    void cancelPending() override {
        if (!m_downloadReply) {
            return;
        }
        disconnect(m_downloadReply, nullptr, this, nullptr);
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
        m_pendingPublicIp.clear();
        m_pendingCallback = {};
        m_pendingDetail.clear();
    }

private:
    GeoIpResolveResult startLocationDatabaseDownload(const QString &publicIp,
                                                     const GeoIpResolveCallback &callback,
                                                     bool forcedUpdate) {
        const QString downloadUrl = m_config.localDb.downloadUrl.trimmed();
        const QUrl url(downloadUrl);
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        m_downloadReply = m_networkManager ? m_networkManager->get(request) : nullptr;
        m_pendingPublicIp = publicIp;
        m_pendingCallback = callback;
        m_pendingDetail =
            forcedUpdate ? QString("Updating local DB from %1").arg(downloadUrl)
                         : QString("Location DB missing at %1; downloading bootstrap from %2")
                               .arg(m_config.localDb.databasePath, downloadUrl);

        if (!m_downloadReply) {
            return {.state = GeoIpResolveState::Unavailable,
                    .detail = QString("Local DB update failed for %1: network manager unavailable").arg(publicIp)};
        }

        connect(m_downloadReply, &QNetworkReply::finished, this, [this, downloadUrl, forcedUpdate]() {
            QPointer<QNetworkReply> reply = m_downloadReply;
            if (!reply) {
                return;
            }
            m_downloadReply = nullptr;

            GeoIpResolveResult result;
            if (reply->error() != QNetworkReply::NoError) {
                result.detail = QString("Local DB update failed for %1: failed to download Geo DB from %2: %3")
                                    .arg(m_pendingPublicIp.isEmpty() ? QString("current runtime") : m_pendingPublicIp,
                                         downloadUrl,
                                         reply->errorString());
            } else {
                const QByteArray payload = reply->readAll();
                if (payload.isEmpty()) {
                    result.detail = QString("Local DB update failed for %1: downloaded empty Geo DB payload from %2")
                                        .arg(m_pendingPublicIp.isEmpty() ? QString("current runtime") : m_pendingPublicIp,
                                             downloadUrl);
                } else {
                    QSaveFile file(m_config.localDb.databasePath);
                    if (!file.open(QIODevice::WriteOnly)) {
                        result.detail = QString("Local DB update failed for %1: failed to open %2 for write")
                                            .arg(m_pendingPublicIp.isEmpty() ? QString("current runtime") : m_pendingPublicIp,
                                                 m_config.localDb.databasePath);
                    } else if (file.write(payload) != payload.size() || !file.commit()) {
                        result.detail = QString("Local DB update failed for %1: failed to persist downloaded Geo DB to %2")
                                            .arg(m_pendingPublicIp.isEmpty() ? QString("current runtime") : m_pendingPublicIp,
                                                 m_config.localDb.databasePath);
                    } else {
                        if (isUsableIpAddress(m_pendingPublicIp)) {
                            result = lookup(m_pendingPublicIp);
                            if (result.state == GeoIpResolveState::Ready) {
                                result.record.source = "local DB";
                                result.detail = forcedUpdate
                                                    ? QString("Updated local DB from %1").arg(downloadUrl)
                                                    : result.detail;
                            }
                        } else {
                            result.state = GeoIpResolveState::Ready;
                            result.detail = QString("Updated local DB from %1").arg(downloadUrl);
                        }
                    }
                }
            }

            const GeoIpResolveCallback callback = m_pendingCallback;
            m_pendingPublicIp.clear();
            m_pendingCallback = {};
            m_pendingDetail.clear();
            if (callback) {
                callback(result);
            }
            reply->deleteLater();
        });

        return {.state = GeoIpResolveState::Pending, .detail = m_pendingDetail};
    }

    GeoIpResolveResult lookup(const QString &publicIp) const {
        MMDB_s cityDb;
        const QByteArray cityPathBytes = m_config.localDb.databasePath.toUtf8();
        int openStatus = MMDB_open(cityPathBytes.constData(), MMDB_MODE_MMAP, &cityDb);
        if (openStatus != MMDB_SUCCESS) {
            return {
                .state = GeoIpResolveState::Unavailable,
                .detail = QString("Location lookup failed for %1: failed to open city DB: %2")
                              .arg(publicIp, QString::fromUtf8(MMDB_strerror(openStatus))),
            };
        }

        const QByteArray ipBytes = publicIp.toUtf8();
        int gaiError = 0;
        int mmdbError = 0;
        const MMDB_lookup_result_s cityLookup = MMDB_lookup_string(&cityDb, ipBytes.constData(), &gaiError, &mmdbError);
        if (gaiError != 0 || mmdbError != MMDB_SUCCESS || !cityLookup.found_entry) {
            MMDB_close(&cityDb);
            return {
                .state = GeoIpResolveState::Unavailable,
                .detail = gaiError != 0 ? QString("Location lookup failed for %1: gai error %2").arg(publicIp).arg(gaiError)
                                        : mmdbError != MMDB_SUCCESS
                                              ? QString("Location lookup failed for %1: %2")
                                                    .arg(publicIp, QString::fromUtf8(MMDB_strerror(mmdbError)))
                                              : QString("Location lookup failed for %1: address not found in Geo DB").arg(publicIp),
            };
        }

        GeoLocationRecord record;
        record.publicIp = publicIp;
        record.city = readMmdbString(&cityLookup.entry, "city", "names", "en");
        record.region = readMmdbString(&cityLookup.entry, "subdivisions", "0", "names", "en");
        record.country = readMmdbString(&cityLookup.entry, "country", "names", "en");
        record.countryCode = readMmdbString(&cityLookup.entry, "country", "iso_code");
        record.timezone = readMmdbString(&cityLookup.entry, "location", "time_zone");
        record.accuracyRadius = readMmdbInt(&cityLookup.entry, "location", "accuracy_radius");

        MMDB_entry_data_s locationData;
        if (MMDB_get_value(const_cast<MMDB_entry_s *>(&cityLookup.entry), &locationData, "location", "latitude", nullptr) ==
                MMDB_SUCCESS &&
            locationData.has_data &&
            locationData.type == MMDB_DATA_TYPE_DOUBLE) {
            record.latitude = locationData.double_value;
            record.hasCoordinates = true;
        }
        if (MMDB_get_value(const_cast<MMDB_entry_s *>(&cityLookup.entry), &locationData, "location", "longitude", nullptr) ==
                MMDB_SUCCESS &&
            locationData.has_data &&
            locationData.type == MMDB_DATA_TYPE_DOUBLE) {
            record.longitude = locationData.double_value;
            record.hasCoordinates = true;
        }

        MMDB_close(&cityDb);

        const QFileInfo asnDbInfo(m_config.localDb.asnDatabasePath);
        if (!m_config.localDb.asnDatabasePath.trimmed().isEmpty() && asnDbInfo.exists() && asnDbInfo.isFile()) {
            MMDB_s asnDb;
            const QByteArray asnPathBytes = m_config.localDb.asnDatabasePath.toUtf8();
            const int asnOpenStatus = MMDB_open(asnPathBytes.constData(), MMDB_MODE_MMAP, &asnDb);
            if (asnOpenStatus == MMDB_SUCCESS) {
                int asnGaiError = 0;
                int asnMmdbError = 0;
                const MMDB_lookup_result_s asnLookup =
                    MMDB_lookup_string(&asnDb, ipBytes.constData(), &asnGaiError, &asnMmdbError);
                if (asnGaiError == 0 && asnMmdbError == MMDB_SUCCESS && asnLookup.found_entry) {
                    const int asnValue = readMmdbInt(&asnLookup.entry, "autonomous_system_number");
                    if (asnValue >= 0) {
                        record.asn = QString::number(asnValue);
                    }
                    record.org = readMmdbString(&asnLookup.entry, "autonomous_system_organization");
                }
                MMDB_close(&asnDb);
            }
        }

        record.provider = "local_db";
        record.providerUrl = m_config.localDb.databasePath;
        record.updatedAt = QFileInfo(m_config.localDb.databasePath).lastModified().toUTC();
        return {
            .state = GeoIpResolveState::Ready,
            .record = record,
            .detail = QString("Lookup IP: %1 · City DB: %2%3")
                          .arg(publicIp,
                               m_config.localDb.databasePath,
                               m_config.localDb.asnDatabasePath.trimmed().isEmpty()
                                   ? QString()
                                   : QString(" · ASN DB: %1").arg(m_config.localDb.asnDatabasePath)),
        };
    }

    config::DiagnosticsLocationConfig m_config;
    QNetworkAccessManager *m_networkManager = nullptr;
    QPointer<QNetworkReply> m_downloadReply;
    QString m_pendingPublicIp;
    GeoIpResolveCallback m_pendingCallback;
    QString m_pendingDetail;
};

class DynamicCachedGeoIpProvider final : public GeoIpProvider {
public:
    DynamicCachedGeoIpProvider(const config::DiagnosticsLocationConfig &config,
                               QNetworkAccessManager *networkManager,
                               QObject *parent = nullptr)
        : GeoIpProvider(parent), m_config(config), m_networkManager(networkManager) {}

    void updateConfig(const config::DiagnosticsLocationConfig &config) override {
        cancelPending();
        m_config = config;
        m_cacheLoaded = false;
        m_cacheError.clear();
        m_cacheRoot = {};
    }

    GeoIpResolveResult readLocation(const QString &publicIp, GeoIpResolveCallback) override {
        if (!isUsableIpAddress(publicIp)) {
            return {.state = GeoIpResolveState::Unavailable, .detail = "Waiting for a usable public IP before resolving location."};
        }

        const QString key = cacheKeyFor(m_config.dynamicCache.provider, publicIp);
        loadCache();
        const QString cacheWarning =
            m_cacheError.isEmpty() ? QString() : QString(" Cache issue: %1.").arg(m_cacheError);

        GeoLocationRecord cachedRecord;
        QJsonObject rawObject;
        const bool hasRecord = readCachedRecord(key, &cachedRecord, &rawObject);
        if (hasRecord) {
            cachedRecord.stale = cachedRecord.nextRefreshAt.isValid() && cachedRecord.nextRefreshAt <= QDateTime::currentDateTimeUtc();
            cachedRecord.source = formatGeoLocationSource(cachedRecord);
        }

        if (hasRecord) {
            return {
                .state = GeoIpResolveState::Ready,
                .record = cachedRecord,
                .detail = cachedRecord.stale
                              ? QString("Loaded stale cached %1 location from %2")
                                    .arg(m_config.dynamicCache.provider, m_config.dynamicCache.cachePath) + cacheWarning
                              : QString("Loaded cached %1 location from %2")
                                    .arg(m_config.dynamicCache.provider, m_config.dynamicCache.cachePath) + cacheWarning,
            };
        }

        return {
            .state = GeoIpResolveState::Unavailable,
            .detail = QString("No cached %1 location entry for %2 in %3")
                          .arg(m_config.dynamicCache.provider, publicIp, m_config.dynamicCache.cachePath) + cacheWarning,
        };
    }

    GeoIpResolveResult refreshLocationData(const QString &publicIp, GeoIpResolveCallback callback) override {
        if (!isUsableIpAddress(publicIp)) {
            return {.state = GeoIpResolveState::Unavailable, .detail = "Waiting for a usable public IP before refreshing location data."};
        }

        const QString key = cacheKeyFor(m_config.dynamicCache.provider, publicIp);
        loadCache();
        const QString cacheWarning =
            m_cacheError.isEmpty() ? QString() : QString(" Cache issue: %1.").arg(m_cacheError);

        GeoLocationRecord cachedRecord;
        QJsonObject rawObject;
        const bool hasRecord = readCachedRecord(key, &cachedRecord, &rawObject);
        if (hasRecord) {
            cachedRecord.stale = cachedRecord.nextRefreshAt.isValid() && cachedRecord.nextRefreshAt <= QDateTime::currentDateTimeUtc();
            cachedRecord.source = formatGeoLocationSource(cachedRecord);
        }

        if (!m_config.dynamicCache.allowManualRefresh) {
            if (hasRecord) {
                return {
                    .state = GeoIpResolveState::Ready,
                    .record = cachedRecord,
                    .detail = QString("Manual location data refresh is disabled; using cached %1 entry from %2")
                                  .arg(m_config.dynamicCache.provider, m_config.dynamicCache.cachePath) + cacheWarning,
                };
            }
            return {.state = GeoIpResolveState::Unavailable,
                    .detail = QString("Manual location data refresh is disabled and no cached location is available.%1").arg(cacheWarning)};
        }

        if (m_reply) {
            m_pendingPublicIp = publicIp;
            m_pendingCallback = callback;
            if (hasRecord) {
                cachedRecord.stale = true;
                return {
                    .state = GeoIpResolveState::Ready,
                    .record = cachedRecord,
                    .detail = QString("Refreshing location data for %1 via %2")
                                  .arg(m_config.dynamicCache.provider, m_config.dynamicCache.url) + cacheWarning,
                };
            }
            return {
                .state = GeoIpResolveState::Pending,
                .detail = QString("Refreshing location data for %1 via %2").arg(m_config.dynamicCache.provider, m_config.dynamicCache.url) +
                          cacheWarning,
            };
        }

        const QUrl url = buildLookupUrl(publicIp);
        if (!url.isValid() || url.scheme().compare("https", Qt::CaseInsensitive) != 0) {
            return {
                .state = GeoIpResolveState::Unavailable,
                .detail = QString("Dynamic GeoIP provider URL must be valid https: %1").arg(m_config.dynamicCache.url),
            };
        }

        if (!m_networkManager) {
            return {.state = GeoIpResolveState::Unavailable, .detail = "Dynamic GeoIP provider is unavailable because the network manager is missing."};
        }

        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        m_reply = m_networkManager->get(request);
        m_pendingPublicIp = publicIp;
        m_pendingCallback = callback;
        m_pendingCachedRecord = hasRecord ? cachedRecord : GeoLocationRecord{};
        m_pendingHasStaleRecord = hasRecord;

        QTimer::singleShot(m_config.dynamicCache.timeoutMs, m_reply, [reply = QPointer<QNetworkReply>(m_reply)]() {
            if (reply && reply->isRunning()) {
                reply->setProperty("timedOut", true);
                reply->abort();
            }
        });

        connect(m_reply, &QNetworkReply::finished, this, [this]() {
            QPointer<QNetworkReply> reply = m_reply;
            if (!reply) {
                return;
            }
            m_reply = nullptr;

            GeoIpResolveResult result;
            if (reply->error() != QNetworkReply::NoError) {
                result = fallbackOrUnavailable(
                    QString("GeoIP provider request failed for %1: %2").arg(m_pendingPublicIp, reply->errorString()));
            } else {
                QString parseFailure;
                const ParsedGeoIpApiResponse parsed = parseIpWhoisResponse(reply->readAll(), m_pendingPublicIp, &parseFailure);
                if (!parsed.ok) {
                    result = fallbackOrUnavailable(QString("GeoIP provider response for %1 was invalid: %2")
                                                       .arg(m_pendingPublicIp, parseFailure));
                } else {
                    GeoLocationRecord record = parsed.record;
                    const QDateTime now = QDateTime::currentDateTimeUtc();
                    record.provider = providerNameForSource(m_config.dynamicCache.provider);
                    record.providerUrl = m_config.dynamicCache.url;
                    record.source = formatGeoLocationSource(record);
                    record.createdAt = m_pendingHasStaleRecord && m_pendingCachedRecord.createdAt.isValid() ? m_pendingCachedRecord.createdAt : now;
                    record.updatedAt = now;
                    record.nextRefreshAt = computeNextRefreshAt(m_config.dynamicCache, record.provider, record.publicIp, now);
                    record.stale = false;
                    QString cacheFailure;
                    persistRecord(cacheKeyFor(m_config.dynamicCache.provider, m_pendingPublicIp), record, parsed.raw, &cacheFailure);
                    result.state = GeoIpResolveState::Ready;
                    result.record = record;
                    result.detail = cacheFailure.isEmpty()
                                        ? QString("Updated %1 location via %2").arg(m_config.dynamicCache.provider, m_config.dynamicCache.url)
                                        : QString("Updated %1 location via %2, but failed to persist cache: %3")
                                              .arg(m_config.dynamicCache.provider, m_config.dynamicCache.url, cacheFailure);
                }
            }

            const GeoIpResolveCallback callback = m_pendingCallback;
            m_pendingPublicIp.clear();
            m_pendingCallback = {};
            m_pendingCachedRecord = {};
            m_pendingHasStaleRecord = false;
            if (callback) {
                callback(result);
            }
            reply->deleteLater();
        });

        if (hasRecord) {
            cachedRecord.stale = true;
            return {
                .state = GeoIpResolveState::Ready,
                .record = cachedRecord,
                .detail = QString("Refreshing location data for %1 via %2")
                              .arg(m_config.dynamicCache.provider, m_config.dynamicCache.url) + cacheWarning,
            };
        }

        return {
            .state = GeoIpResolveState::Pending,
            .detail = QString("Refreshing location data for %1 via %2").arg(m_config.dynamicCache.provider, m_config.dynamicCache.url) +
                      cacheWarning,
        };
    }

    void cancelPending() override {
        if (!m_reply) {
            return;
        }
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
        m_pendingPublicIp.clear();
        m_pendingCallback = {};
        m_pendingCachedRecord = {};
        m_pendingHasStaleRecord = false;
    }

private:
    void loadCache() {
        if (m_cacheLoaded) {
            return;
        }

        m_cacheLoaded = true;
        m_cacheRoot = QJsonObject{{"schema_version", kGeoIpCacheSchemaVersion}, {"records", QJsonObject{}}};
        m_cacheError.clear();

        QFile file(m_config.dynamicCache.cachePath);
        if (!file.exists()) {
            return;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            m_cacheError = QString("Failed to open GeoIP cache %1").arg(m_config.dynamicCache.cachePath);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
            m_cacheError = QString("GeoIP cache %1 is invalid JSON").arg(m_config.dynamicCache.cachePath);
            return;
        }

        m_cacheRoot = json.object();
        if (m_cacheRoot.value("schema_version").toInt(kGeoIpCacheSchemaVersion) != kGeoIpCacheSchemaVersion ||
            !m_cacheRoot.value("records").isObject()) {
            m_cacheError = QString("GeoIP cache %1 uses an incompatible schema").arg(m_config.dynamicCache.cachePath);
            m_cacheRoot = QJsonObject{{"schema_version", kGeoIpCacheSchemaVersion}, {"records", QJsonObject{}}};
        }
    }

    bool readCachedRecord(const QString &key, GeoLocationRecord *record, QJsonObject *raw) {
        const QJsonObject records = m_cacheRoot.value("records").toObject();
        const QJsonValue value = records.value(key);
        if (!value.isObject()) {
            return false;
        }

        QString failureReason;
        if (!recordFromJson(value.toObject(), record, raw, &failureReason)) {
            m_cacheError = QString("GeoIP cache record %1 is invalid: %2").arg(key, failureReason);
            return false;
        }
        if (record->provider != providerNameForSource(m_config.dynamicCache.provider) || record->providerUrl != m_config.dynamicCache.url) {
            return false;
        }
        return true;
    }

    void persistRecord(const QString &key, const GeoLocationRecord &record, const QJsonObject &raw, QString *failureReason) {
        loadCache();

        QFileInfo cacheInfo(m_config.dynamicCache.cachePath);
        QDir cacheDir = cacheInfo.dir();
        if (!cacheDir.exists() && !cacheDir.mkpath(".")) {
            if (failureReason) {
                *failureReason = QString("failed to create cache directory %1").arg(cacheDir.absolutePath());
            }
            return;
        }

        QJsonObject records = m_cacheRoot.value("records").toObject();
        records.insert(key, recordToJson(record, raw));
        m_cacheRoot.insert("schema_version", kGeoIpCacheSchemaVersion);
        m_cacheRoot.insert("records", records);

        QSaveFile file(m_config.dynamicCache.cachePath);
        if (!file.open(QIODevice::WriteOnly)) {
            if (failureReason) {
                *failureReason = QString("failed to open %1 for write").arg(m_config.dynamicCache.cachePath);
            }
            return;
        }

        const QByteArray payload = QJsonDocument(m_cacheRoot).toJson(QJsonDocument::Indented);
        if (file.write(payload) != payload.size() || !file.commit()) {
            if (failureReason) {
                *failureReason = QString("failed to persist %1").arg(m_config.dynamicCache.cachePath);
            }
            return;
        }

        if (failureReason) {
            failureReason->clear();
        }
        m_cacheError.clear();
    }

    QUrl buildLookupUrl(const QString &publicIp) const {
        QUrl url(m_config.dynamicCache.url);
        QString path = url.path();
        if (!path.endsWith('/')) {
            path += '/';
        }
        path += publicIp;
        url.setPath(path);
        return url;
    }

    GeoIpResolveResult fallbackOrUnavailable(const QString &failureDetail) const {
        if (m_pendingHasStaleRecord) {
            GeoLocationRecord staleRecord = m_pendingCachedRecord;
            staleRecord.stale = true;
            return {
                .state = GeoIpResolveState::Ready,
                .record = staleRecord,
                .detail = QString("%1; using stale cached location").arg(failureDetail),
            };
        }
        return {.state = GeoIpResolveState::Unavailable, .detail = failureDetail};
    }

    config::DiagnosticsLocationConfig m_config;
    QNetworkAccessManager *m_networkManager = nullptr;
    bool m_cacheLoaded = false;
    QString m_cacheError;
    QJsonObject m_cacheRoot;
    QPointer<QNetworkReply> m_reply;
    QString m_pendingPublicIp;
    GeoIpResolveCallback m_pendingCallback;
    GeoLocationRecord m_pendingCachedRecord;
    bool m_pendingHasStaleRecord = false;
};

}  // namespace

std::unique_ptr<GeoIpProvider> createGeoIpProvider(const config::DiagnosticsLocationConfig &config,
                                                   QNetworkAccessManager *networkManager,
                                                   QObject *parent) {
    switch (config.mode) {
    case config::DiagnosticsLocationMode::Disabled:
        return std::make_unique<DisabledGeoIpProvider>(parent);
    case config::DiagnosticsLocationMode::DynamicCache:
        return std::make_unique<DynamicCachedGeoIpProvider>(config, networkManager, parent);
    case config::DiagnosticsLocationMode::LocalDb:
    default:
        return std::make_unique<LocalMmdbGeoIpProvider>(config, networkManager, parent);
    }
}

QString formatGeoLocationSummary(const GeoLocationRecord &record) {
    QStringList parts;
    if (!record.city.isEmpty()) {
        parts.push_back(record.city);
    } else if (!record.region.isEmpty()) {
        parts.push_back(record.region);
    }

    if (!record.countryCode.isEmpty()) {
        parts.push_back(record.countryCode);
    } else if (!record.country.isEmpty()) {
        parts.push_back(record.country);
    }

    return parts.isEmpty() ? QString("Unknown location") : parts.join(", ");
}

QString formatGeoLocationAsnOrg(const GeoLocationRecord &record) {
    QStringList parts;
    if (!record.asn.trimmed().isEmpty()) {
        const QString asn = record.asn.startsWith("AS", Qt::CaseInsensitive) ? record.asn : QString("AS%1").arg(record.asn);
        parts.push_back(asn);
    }
    if (!record.org.trimmed().isEmpty()) {
        parts.push_back(record.org.trimmed());
    } else if (!record.isp.trimmed().isEmpty()) {
        parts.push_back(record.isp.trimmed());
    }

    return parts.isEmpty() ? QString() : parts.join(" ");
}

QString formatGeoLocationSource(const GeoLocationRecord &record) {
    if (record.provider.compare("local_db", Qt::CaseInsensitive) == 0) {
        return "local DB";
    }
    if (!record.provider.isEmpty()) {
        return QString("dynamic cache / %1").arg(providerNameForSource(record.provider));
    }
    return record.source;
}

}  // namespace tunlet::diagnostics
