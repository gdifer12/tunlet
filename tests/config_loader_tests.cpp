#include "config/config_loader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

TEST_CASE("ConfigLoader parses valid config", "[config]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString configPath = dir.path() + "/config.yaml";
    QFile file(configPath);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream(&file);
    stream
        << "clashApi:\n"
        << "  host: 127.0.0.1\n"
        << "  port: 9090\n"
        << "  profiles:\n"
        << "    - name: gaming\n"
        << "      mode: gaming\n"
        << "      desc: Gaming profile\n"
        << "ruleSets:\n"
        << "  forceProxyPath: /tmp/force-proxy.json\n"
        << "  forceDirectPath: /tmp/force-direct.json\n"
        << "  autoProxyPath: /tmp/auto-proxy.json\n"
        << "  autoDirectPath: /tmp/auto-direct.json\n"
        << "diagnostics:\n"
        << "  enabled: true\n"
        << "  refreshIntervalMs: 1234\n"
        << "  requestTimeoutMs: 4321\n"
        << "  externalIp:\n"
        << "    enabled: true\n"
        << "    proxyUrl: https://ifconfig.me\n"
        << "    locationUrlTemplate: https://ipwho.is/{ip}\n"
        << "editing:\n"
        << "  createBackup: false\n";
    file.close();

    const auto config = tunlet::config::ConfigLoader::loadFromPath(configPath);
    REQUIRE(config.clashApi.host == "127.0.0.1");
    REQUIRE(config.clashApi.port == 9090);
    REQUIRE(config.clashApi.profiles.size() == 4);
    REQUIRE(config.clashApi.profiles.at(0).name == "direct");
    REQUIRE(config.clashApi.profiles.at(0).mode == "direct");
    REQUIRE(config.clashApi.profiles.at(1).name == "proxy");
    REQUIRE(config.clashApi.profiles.at(1).mode == "global");
    REQUIRE(config.clashApi.profiles.at(2).name == "auto");
    REQUIRE(config.clashApi.profiles.at(2).mode == "rule");
    REQUIRE(config.clashApi.profiles.at(3).name == "gaming");
    REQUIRE(config.clashApi.profiles.at(3).mode == "gaming");
    REQUIRE(config.clashApi.profiles.at(3).desc == "Gaming profile");
    REQUIRE(config.diagnostics.refreshIntervalMs == 1234);
    REQUIRE(config.diagnostics.externalIp.enabled == true);
    REQUIRE(config.diagnostics.externalIp.locationUrlTemplate == "https://ipwho.is/{ip}");
    REQUIRE(config.editing.createBackup == false);
}

TEST_CASE("ConfigLoader rejects missing ruleSets", "[config]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString configPath = dir.path() + "/config.yaml";
    QFile file(configPath);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream(&file);
    stream
        << "clashApi:\n"
        << "  host: 127.0.0.1\n"
        << "  port: 9090\n";
    file.close();

    REQUIRE_THROWS(tunlet::config::ConfigLoader::loadFromPath(configPath));
}
