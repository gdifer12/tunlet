#include "logging/logging_service.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace {

tunlet::config::AppConfig makeConfig(const QString &route) {
    tunlet::config::AppConfig config;
    config.configRoute = route;
    return config;
}

QString readUtf8File(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QJsonObject readFirstJsonLine(const QString &path) {
    const QString payload = readUtf8File(path).trimmed();
    if (payload.isEmpty()) {
        return {};
    }
    return QJsonDocument::fromJson(payload.toUtf8()).object();
}

QString archivePath(const QString &basePath, int index) {
    return QString("%1.%2").arg(basePath).arg(index);
}

}  // namespace

TEST_CASE("LoggingService stays disabled by default", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const auto config = makeConfig(dir.path());
    tunlet::logging::LoggingService service(config);

    service.logInfo("test", "disabled logger should not write");

    const auto status = service.status();
    REQUIRE(status.enabled == false);
    REQUIRE(status.textSinkActive == false);
    REQUIRE(status.jsonlSinkActive == false);
}

TEST_CASE("LoggingService writes readable text logs", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    auto config = makeConfig(dir.path());
    config.logging.enabled = true;
    config.logging.textPath = dir.path() + "/tunlet.log";

    tunlet::logging::LoggingService service(config);
    service.logInfo("mode.switch",
                    "Requested mode switch",
                    "Switching to proxy",
                    {{"requested_profile", "proxy"}, {"backend_mode", "global"}});

    const QString payload = readUtf8File(config.logging.textPath);
    REQUIRE(payload.contains("INFO"));
    REQUIRE(payload.contains("[mode.switch]"));
    REQUIRE(payload.contains("Requested mode switch"));
    REQUIRE(payload.contains("detail=\"Switching to proxy\""));
    REQUIRE(payload.contains("requested_profile=proxy"));
}

TEST_CASE("LoggingService writes JSONL logs", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    auto config = makeConfig(dir.path());
    config.logging.enabled = true;
    config.logging.jsonlPath = dir.path() + "/tunlet.jsonl";

    tunlet::logging::LoggingService service(config);
    service.logWarning("diagnostics.location",
                       "Location data refresh failed",
                       "provider timeout",
                       {{"public_ip", "203.0.113.8"}});

    const QJsonObject object = readFirstJsonLine(config.logging.jsonlPath);
    REQUIRE(object.value("level").toString() == "warning");
    REQUIRE(object.value("source").toString() == "diagnostics.location");
    REQUIRE(object.value("summary").toString() == "Location data refresh failed");
    REQUIRE(object.value("detail").toString() == "provider timeout");
    REQUIRE(object.value("context").toObject().value("public_ip").toString() == "203.0.113.8");
}

TEST_CASE("LoggingService filters entries below the configured level", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    auto config = makeConfig(dir.path());
    config.logging.enabled = true;
    config.logging.level = tunlet::config::LoggingLevel::Warning;
    config.logging.textPath = dir.path() + "/tunlet.log";

    tunlet::logging::LoggingService service(config);
    service.logInfo("test", "info entry");
    service.logWarning("test", "warning entry");

    const QString payload = readUtf8File(config.logging.textPath);
    REQUIRE_FALSE(payload.contains("info entry"));
    REQUIRE(payload.contains("warning entry"));
}

TEST_CASE("LoggingService supports dual sinks", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    auto config = makeConfig(dir.path());
    config.logging.enabled = true;
    config.logging.textPath = dir.path() + "/tunlet.log";
    config.logging.jsonlPath = dir.path() + "/tunlet.jsonl";

    tunlet::logging::LoggingService service(config);
    service.logError("config.apply", "Runtime apply failed", "permission denied");

    REQUIRE(readUtf8File(config.logging.textPath).contains("Runtime apply failed"));
    REQUIRE(readUtf8File(config.logging.jsonlPath).contains("\"summary\":\"Runtime apply failed\""));

    const auto status = service.status();
    REQUIRE(status.textSinkActive);
    REQUIRE(status.jsonlSinkActive);
}

TEST_CASE("LoggingService keeps append-only behavior when rotation is disabled", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    auto config = makeConfig(dir.path());
    config.logging.enabled = true;
    config.logging.textPath = dir.path() + "/tunlet.log";
    config.logging.rotation.enabled = false;
    config.logging.rotation.maxFileBytes = 64;
    config.logging.rotation.keepFiles = 2;

    tunlet::logging::LoggingService service(config);
    service.logInfo("test", "first entry", QString(80, 'a'));
    service.logInfo("test", "second entry", QString(80, 'b'));

    REQUIRE(QFile::exists(config.logging.textPath));
    REQUIRE_FALSE(QFile::exists(archivePath(config.logging.textPath, 1)));
    REQUIRE(readUtf8File(config.logging.textPath).contains("second entry"));
}

TEST_CASE("LoggingService keeps healthy sink active when another sink fails", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    auto config = makeConfig(dir.path());
    config.logging.enabled = true;
    config.logging.textPath = dir.path();
    config.logging.jsonlPath = dir.path() + "/tunlet.jsonl";

    tunlet::logging::LoggingService service(config);
    service.logWarning("test", "warn entry");

    const auto status = service.status();
    REQUIRE(status.textSinkActive == false);
    REQUIRE(status.jsonlSinkActive == true);
    REQUIRE_FALSE(status.lastError.isEmpty());
    REQUIRE(readUtf8File(config.logging.jsonlPath).contains("\"summary\":\"warn entry\""));
}

TEST_CASE("LoggingService reports when all sinks are unavailable", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    auto config = makeConfig(dir.path());
    config.logging.enabled = true;
    config.logging.textPath = dir.path();
    config.logging.jsonlPath = dir.path();

    tunlet::logging::LoggingService service(config);

    const auto status = service.status();
    REQUIRE(status.enabled == true);
    REQUIRE(status.textSinkActive == false);
    REQUIRE(status.jsonlSinkActive == false);
    REQUIRE_FALSE(status.lastError.isEmpty());
}

TEST_CASE("LoggingService updates sinks and level live", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    auto config = makeConfig(dir.path());
    config.logging.enabled = true;
    config.logging.textPath = dir.path() + "/one.log";

    tunlet::logging::LoggingService service(config);
    service.logInfo("test", "before reconfigure");

    auto updated = config;
    updated.logging.level = tunlet::config::LoggingLevel::Error;
    updated.logging.textPath = dir.path() + "/two.log";
    service.updateConfig(updated);

    service.logWarning("test", "suppressed warning");
    service.logError("test", "after reconfigure");

    REQUIRE(readUtf8File(config.logging.textPath).contains("before reconfigure"));
    REQUIRE_FALSE(readUtf8File(config.logging.textPath).contains("after reconfigure"));
    REQUIRE_FALSE(readUtf8File(updated.logging.textPath).contains("suppressed warning"));
    REQUIRE(readUtf8File(updated.logging.textPath).contains("after reconfigure"));

    auto disabled = updated;
    disabled.logging.enabled = false;
    service.updateConfig(disabled);
    service.logError("test", "disabled write");
    REQUIRE_FALSE(readUtf8File(updated.logging.textPath).contains("disabled write"));
}

TEST_CASE("LoggingService rotates text logs when the next write would exceed the limit", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    auto config = makeConfig(dir.path());
    config.logging.enabled = true;
    config.logging.textPath = dir.path() + "/tunlet.log";
    config.logging.rotation.enabled = true;
    config.logging.rotation.maxFileBytes = 140;
    config.logging.rotation.keepFiles = 2;

    tunlet::logging::LoggingService service(config);
    service.logInfo("test", "first entry", QString(80, 'a'));
    service.logInfo("test", "second entry", QString(80, 'b'));
    service.logInfo("test", "third entry", QString(80, 'c'));

    REQUIRE(readUtf8File(config.logging.textPath).contains("third entry"));
    REQUIRE(readUtf8File(archivePath(config.logging.textPath, 1)).contains("second entry"));
    REQUIRE(readUtf8File(archivePath(config.logging.textPath, 2)).contains("first entry"));
    REQUIRE_FALSE(QFile::exists(archivePath(config.logging.textPath, 3)));
}

TEST_CASE("LoggingService rotates JSONL logs independently under the same policy", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    auto config = makeConfig(dir.path());
    config.logging.enabled = true;
    config.logging.jsonlPath = dir.path() + "/tunlet.jsonl";
    config.logging.rotation.enabled = true;
    config.logging.rotation.maxFileBytes = 150;
    config.logging.rotation.keepFiles = 2;

    tunlet::logging::LoggingService service(config);
    service.logWarning("test", "entry one", QString(80, 'a'));
    service.logWarning("test", "entry two", QString(80, 'b'));

    REQUIRE(readFirstJsonLine(config.logging.jsonlPath).value("summary").toString() == "entry two");
    REQUIRE(readFirstJsonLine(archivePath(config.logging.jsonlPath, 1)).value("summary").toString() == "entry one");
}

TEST_CASE("LoggingService rotates oversized file immediately on startup", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    auto config = makeConfig(dir.path());
    config.logging.enabled = true;
    config.logging.textPath = dir.path() + "/tunlet.log";
    config.logging.rotation.enabled = true;
    config.logging.rotation.maxFileBytes = 32;
    config.logging.rotation.keepFiles = 2;

    QFile seed(config.logging.textPath);
    REQUIRE(seed.open(QIODevice::WriteOnly | QIODevice::Text));
    seed.write("this payload already exceeds the configured startup rotation limit");
    seed.close();

    tunlet::logging::LoggingService service(config);

    REQUIRE(QFile::exists(archivePath(config.logging.textPath, 1)));
    REQUIRE(readUtf8File(archivePath(config.logging.textPath, 1)).contains("already exceeds"));
    REQUIRE(readUtf8File(config.logging.textPath).isEmpty());
}

TEST_CASE("LoggingService updates rotation policy live", "[logging]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    auto config = makeConfig(dir.path());
    config.logging.enabled = true;
    config.logging.textPath = dir.path() + "/tunlet.log";

    tunlet::logging::LoggingService service(config);
    service.logInfo("test", "before rotation");

    auto updated = config;
    updated.logging.rotation.enabled = true;
    updated.logging.rotation.maxFileBytes = 120;
    updated.logging.rotation.keepFiles = 1;
    service.updateConfig(updated);
    service.logInfo("test", "rotate me", QString(80, 'x'));
    service.logInfo("test", "after rotation", QString(80, 'y'));

    REQUIRE(readUtf8File(updated.logging.textPath).contains("after rotation"));
    REQUIRE(readUtf8File(archivePath(updated.logging.textPath, 1)).contains("rotate me"));

    auto disabled = updated;
    disabled.logging.rotation.enabled = false;
    service.updateConfig(disabled);
    service.logInfo("test", "append after disable", QString(80, 'z'));

    REQUIRE(readUtf8File(updated.logging.textPath).contains("append after disable"));
}
