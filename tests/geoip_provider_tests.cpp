#include "config/app_config.hpp"
#include "diagnostics/geoip_provider.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <catch2/catch_test_macros.hpp>

#include <cstring>

namespace {

struct FakeResponse {
    QByteArray body;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QString errorString;
};

QCoreApplication *ensureCoreApplication() {
    if (QCoreApplication::instance()) {
        return QCoreApplication::instance();
    }

    static int argc = 1;
    static char appName[] = "tunlet_tests";
    static char *argv[] = {appName, nullptr};
    return new QCoreApplication(argc, argv);
}

QString cacheKeyForTest(const QString &provider, const QString &publicIp) {
    return QString("%1:%2").arg(provider.trimmed().toLower(), publicIp.trimmed());
}

QJsonObject makeCacheRecord(const QString &provider,
                            const QString &providerUrl,
                            const QString &publicIp,
                            const QDateTime &updatedAt,
                            const QDateTime &nextRefreshAt) {
    return QJsonObject{
        {"provider", provider},
        {"provider_url", providerUrl},
        {"public_ip", publicIp},
        {"schema_version", 1},
        {"created_at", updatedAt.addDays(-1).toUTC().toString(Qt::ISODate)},
        {"updated_at", updatedAt.toUTC().toString(Qt::ISODate)},
        {"next_refresh_at", nextRefreshAt.toUTC().toString(Qt::ISODate)},
        {"normalized",
         QJsonObject{
             {"country", "Netherlands"},
             {"country_code", "NL"},
             {"region", "North Holland"},
             {"city", "Amsterdam"},
             {"timezone", "Europe/Amsterdam"},
             {"asn", "12345"},
             {"org", "Example Org"},
             {"isp", "Example ISP"},
         }},
        {"raw", QJsonObject{{"success", true}}},
    };
}

void writeCacheFile(const QString &cachePath,
                    const QString &provider,
                    const QString &providerUrl,
                    const QString &publicIp,
                    const QDateTime &updatedAt,
                    const QDateTime &nextRefreshAt) {
    QJsonObject records;
    records.insert(cacheKeyForTest(provider, publicIp),
                   makeCacheRecord(provider, providerUrl, publicIp, updatedAt, nextRefreshAt));
    QFile file(cachePath);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(QJsonObject{{"schema_version", 1}, {"records", records}}).toJson(QJsonDocument::Indented));
    file.close();
}

tunlet::config::DiagnosticsLocationConfig makeDynamicCacheConfig(const QString &cachePath, bool allowManualRefresh = true) {
    tunlet::config::DiagnosticsLocationConfig config;
    config.mode = tunlet::config::DiagnosticsLocationMode::DynamicCache;
    config.dynamicCache.provider = "ipwhois";
    config.dynamicCache.url = "https://ipwho.is/";
    config.dynamicCache.cachePath = cachePath;
    config.dynamicCache.baseRefreshDays = 14;
    config.dynamicCache.randomShiftDays = 0;
    config.dynamicCache.timeoutMs = 1000;
    config.dynamicCache.allowManualRefresh = allowManualRefresh;
    return config;
}

class FakeNetworkReply final : public QNetworkReply {
public:
    FakeNetworkReply(const QNetworkRequest &request,
                     QNetworkAccessManager::Operation operation,
                     const FakeResponse &response,
                     QObject *parent = nullptr)
        : QNetworkReply(parent), m_body(response.body), m_error(response.error), m_errorString(response.errorString) {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QTimer::singleShot(0, this, [this]() {
            if (m_error != QNetworkReply::NoError) {
                setError(m_error, m_errorString);
            } else {
                setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
            }
            setFinished(true);
            if (!m_body.isEmpty()) {
                emit readyRead();
            }
            emit finished();
        });
    }

    void abort() override {
        if (isFinished()) {
            return;
        }
        setError(QNetworkReply::OperationCanceledError, "aborted");
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override {
        return m_body.size() - m_offset + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxlen) override {
        if (m_offset >= m_body.size()) {
            return -1;
        }

        const qint64 bytesToCopy = qMin(maxlen, m_body.size() - m_offset);
        memcpy(data, m_body.constData() + m_offset, static_cast<size_t>(bytesToCopy));
        m_offset += bytesToCopy;
        return bytesToCopy;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
    QNetworkReply::NetworkError m_error = QNetworkReply::NoError;
    QString m_errorString;
};

class FakeNetworkAccessManager final : public QNetworkAccessManager {
public:
    QList<FakeResponse> responses;
    QList<QUrl> requestedUrls;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *outgoingData) override {
        Q_UNUSED(outgoingData);
        requestedUrls.push_back(request.url());
        const FakeResponse response = responses.isEmpty() ? FakeResponse{} : responses.takeFirst();
        return new FakeNetworkReply(request, operation, response, this);
    }
};

}  // namespace

TEST_CASE("Dynamic cache normal read stays local on cache hit and cache miss", "[geoip]") {
    ensureCoreApplication();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString cachePath = dir.filePath("geoip-cache.json");
    const QString publicIp = "198.51.100.10";
    const QDateTime now = QDateTime::currentDateTimeUtc();
    writeCacheFile(cachePath, "ipwhois", "https://ipwho.is/", publicIp, now, now.addDays(1));

    FakeNetworkAccessManager networkManager;
    auto provider = tunlet::diagnostics::createGeoIpProvider(makeDynamicCacheConfig(cachePath), &networkManager);

    const auto hitResult = provider->readLocation(publicIp, {});
    REQUIRE(hitResult.state == tunlet::diagnostics::GeoIpResolveState::Ready);
    REQUIRE(hitResult.record.publicIp == publicIp);
    REQUIRE(networkManager.requestedUrls.isEmpty());

    const auto missResult = provider->readLocation("198.51.100.11", {});
    REQUIRE(missResult.state == tunlet::diagnostics::GeoIpResolveState::Unavailable);
    REQUIRE(missResult.detail.contains("No cached"));
    REQUIRE(networkManager.requestedUrls.isEmpty());
}

TEST_CASE("Dynamic cache bootstraps and persists a missing entry", "[geoip]") {
    ensureCoreApplication();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString cachePath = dir.filePath("geoip-cache.json");
    const QString publicIp = "203.0.113.20";

    FakeNetworkAccessManager networkManager;
    networkManager.responses.push_back(FakeResponse{
        .body = R"({"success":true,"country":"Netherlands","country_code":"NL","region":"North Holland","city":"Amsterdam","latitude":52.37,"longitude":4.89,"timezone":{"id":"Europe/Amsterdam"},"connection":{"asn":12345,"org":"Example Org","isp":"Example ISP"}})",
    });

    auto provider = tunlet::diagnostics::createGeoIpProvider(makeDynamicCacheConfig(cachePath), &networkManager);

    tunlet::diagnostics::GeoIpResolveResult immediate;
    tunlet::diagnostics::GeoIpResolveResult asyncResult;
    bool callbackCalled = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(1000);

    immediate = provider->bootstrapLocationOnMiss(publicIp, [&](const tunlet::diagnostics::GeoIpResolveResult &result) {
        callbackCalled = true;
        asyncResult = result;
        loop.quit();
    });

    REQUIRE(immediate.state == tunlet::diagnostics::GeoIpResolveState::Pending);
    REQUIRE(immediate.detail.contains("bootstrapping"));
    REQUIRE(networkManager.requestedUrls.size() == 1);
    REQUIRE(networkManager.requestedUrls.first() == QUrl("https://ipwho.is/203.0.113.20"));

    loop.exec();
    REQUIRE(callbackCalled);
    REQUIRE(asyncResult.state == tunlet::diagnostics::GeoIpResolveState::Ready);
    REQUIRE(asyncResult.record.publicIp == publicIp);
    REQUIRE(asyncResult.detail.contains("Created cached"));

    QFile cacheFile(cachePath);
    REQUIRE(cacheFile.open(QIODevice::ReadOnly));
    const QJsonDocument cacheJson = QJsonDocument::fromJson(cacheFile.readAll());
    const QJsonObject records = cacheJson.object().value("records").toObject();
    const QJsonObject record = records.value(cacheKeyForTest("ipwhois", publicIp)).toObject();
    REQUIRE(!record.isEmpty());
    REQUIRE(record.value("public_ip").toString() == publicIp);
}

TEST_CASE("Dynamic cache bootstrap ignores manual refresh disable on cache miss", "[geoip]") {
    ensureCoreApplication();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString cachePath = dir.filePath("geoip-cache.json");
    const QString publicIp = "203.0.113.21";

    FakeNetworkAccessManager networkManager;
    networkManager.responses.push_back(FakeResponse{
        .body = R"({"success":true,"country":"Netherlands","country_code":"NL","region":"North Holland","city":"Amsterdam","timezone":{"id":"Europe/Amsterdam"},"connection":{"asn":12345,"org":"Example Org","isp":"Example ISP"}})",
    });

    auto provider =
        tunlet::diagnostics::createGeoIpProvider(makeDynamicCacheConfig(cachePath, /*allowManualRefresh=*/false), &networkManager);

    tunlet::diagnostics::GeoIpResolveResult immediate;
    tunlet::diagnostics::GeoIpResolveResult asyncResult;
    bool callbackCalled = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(1000);

    immediate = provider->bootstrapLocationOnMiss(publicIp, [&](const tunlet::diagnostics::GeoIpResolveResult &result) {
        callbackCalled = true;
        asyncResult = result;
        loop.quit();
    });

    REQUIRE(immediate.state == tunlet::diagnostics::GeoIpResolveState::Pending);
    loop.exec();
    REQUIRE(callbackCalled);
    REQUIRE(asyncResult.state == tunlet::diagnostics::GeoIpResolveState::Ready);
    REQUIRE(networkManager.requestedUrls.size() == 1);
}

TEST_CASE("Dynamic cache bootstrap failure leaves missing entry unavailable", "[geoip]") {
    ensureCoreApplication();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString cachePath = dir.filePath("geoip-cache.json");
    const QString publicIp = "203.0.113.22";

    FakeNetworkAccessManager networkManager;
    networkManager.responses.push_back(FakeResponse{
        .error = QNetworkReply::HostNotFoundError,
        .errorString = "host not found",
    });

    auto provider = tunlet::diagnostics::createGeoIpProvider(makeDynamicCacheConfig(cachePath), &networkManager);

    tunlet::diagnostics::GeoIpResolveResult immediate;
    tunlet::diagnostics::GeoIpResolveResult asyncResult;
    bool callbackCalled = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(1000);

    immediate = provider->bootstrapLocationOnMiss(publicIp, [&](const tunlet::diagnostics::GeoIpResolveResult &result) {
        callbackCalled = true;
        asyncResult = result;
        loop.quit();
    });

    REQUIRE(immediate.state == tunlet::diagnostics::GeoIpResolveState::Pending);
    loop.exec();
    REQUIRE(callbackCalled);
    REQUIRE(asyncResult.state == tunlet::diagnostics::GeoIpResolveState::Unavailable);
    REQUIRE(asyncResult.detail.contains("request failed"));
    REQUIRE(!QFileInfo::exists(cachePath));
}

TEST_CASE("Dynamic cache manual refresh semantics remain unchanged", "[geoip]") {
    ensureCoreApplication();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString cachePath = dir.filePath("geoip-cache.json");
    const QString publicIp = "198.51.100.30";
    const QDateTime now = QDateTime::currentDateTimeUtc();
    writeCacheFile(cachePath, "ipwhois", "https://ipwho.is/", publicIp, now, now.addDays(1));

    SECTION("manual refresh disabled uses cached hit and skips network") {
        FakeNetworkAccessManager networkManager;
        auto provider =
            tunlet::diagnostics::createGeoIpProvider(makeDynamicCacheConfig(cachePath, /*allowManualRefresh=*/false), &networkManager);

        const auto result = provider->refreshLocationData(publicIp, {});
        REQUIRE(result.state == tunlet::diagnostics::GeoIpResolveState::Ready);
        REQUIRE(result.detail.contains("Manual location data refresh is disabled"));
        REQUIRE(networkManager.requestedUrls.isEmpty());
    }

    SECTION("manual refresh disabled with missing cache stays unavailable and skips network") {
        FakeNetworkAccessManager networkManager;
        auto provider =
            tunlet::diagnostics::createGeoIpProvider(makeDynamicCacheConfig(cachePath, /*allowManualRefresh=*/false), &networkManager);

        const auto result = provider->refreshLocationData("198.51.100.31", {});
        REQUIRE(result.state == tunlet::diagnostics::GeoIpResolveState::Unavailable);
        REQUIRE(result.detail.contains("Manual location data refresh is disabled"));
        REQUIRE(networkManager.requestedUrls.isEmpty());
    }

    SECTION("manual refresh with cached entry keeps stale fallback while request runs") {
        FakeNetworkAccessManager networkManager;
        networkManager.responses.push_back(FakeResponse{
            .body = R"({"success":true,"country":"Netherlands","country_code":"NL","region":"North Holland","city":"Amsterdam","timezone":{"id":"Europe/Amsterdam"},"connection":{"asn":12345,"org":"Example Org","isp":"Example ISP"}})",
        });
        auto provider = tunlet::diagnostics::createGeoIpProvider(makeDynamicCacheConfig(cachePath), &networkManager);

        tunlet::diagnostics::GeoIpResolveResult immediate;
        tunlet::diagnostics::GeoIpResolveResult asyncResult;
        bool callbackCalled = false;
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(1000);

        immediate = provider->refreshLocationData(publicIp, [&](const tunlet::diagnostics::GeoIpResolveResult &result) {
            callbackCalled = true;
            asyncResult = result;
            loop.quit();
        });

        REQUIRE(immediate.state == tunlet::diagnostics::GeoIpResolveState::Ready);
        REQUIRE(immediate.record.stale);
        REQUIRE(immediate.detail.contains("Refreshing location data"));
        loop.exec();
        REQUIRE(callbackCalled);
        REQUIRE(asyncResult.state == tunlet::diagnostics::GeoIpResolveState::Ready);
        REQUIRE(asyncResult.detail.contains("Updated"));
    }
}
