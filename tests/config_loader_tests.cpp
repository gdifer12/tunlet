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
        << "configRoute: /tmp/tunlet-root\n"
        << "clashApi:\n"
        << "  host: 127.0.0.1\n"
        << "  port: 9090\n"
        << "  disableDefaultProfiles: false\n"
        << "  profiles:\n"
        << "    - name: gaming\n"
        << "      mode: gaming\n"
        << "      desc: Gaming profile\n"
        << "ruleSets:\n"
        << "  forceProxyPath: sing-box/force-proxy.json\n"
        << "  forceDirectPath: sing-box/force-direct.json\n"
        << "  autoProxyPath: sing-box/auto-proxy.json\n"
        << "  autoDirectPath: sing-box/auto-direct.json\n"
        << "diagnostics:\n"
        << "  enabled: true\n"
        << "  refreshIntervalMs: 1234\n"
        << "  requestTimeoutMs: 4321\n"
        << "  connection:\n"
        << "    ipv4:\n"
        << "      executable: curl\n"
        << "      args: [-4, -sS, --noproxy, '*', https://api.ipify.org]\n"
        << "    timing:\n"
        << "      executable: curl\n"
        << "      args: [-4, -sS, --noproxy, '*', -o, /dev/null, -w, 'dns=%{time_namelookup}s connect=%{time_connect}s tls=%{time_appconnect}s total=%{time_total}s\\n', https://www.gstatic.com/generate_204]\n"
        << "    dns:\n"
        << "      executable: dig\n"
        << "      args: [-4, +short, TXT, o-o.myaddr.l.google.com]\n"
        << "    location:\n"
        << "      enabled: true\n"
        << "      databasePath: geo/GeoLite2-City.mmdb\n"
        << "      downloadUrl: https://example.test/GeoLite2-City.mmdb\n"
        << "editing:\n"
        << "  createBackup: false\n"
        << "tray:\n"
        << "  keepRunningWithoutWindow: false\n"
        << "  startHidden: true\n";
    file.close();

    const auto config = tunlet::config::ConfigLoader::loadFromPath(configPath);
    REQUIRE(config.clashApi.host == "127.0.0.1");
    REQUIRE(config.clashApi.port == 9090);
    REQUIRE(config.clashApi.disableDefaultProfiles == false);
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
    REQUIRE(config.configRoute == "/tmp/tunlet-root");
    REQUIRE(config.ruleSets.forceProxyPath == "/tmp/tunlet-root/sing-box/force-proxy.json");
    REQUIRE(config.diagnostics.refreshIntervalMs == 1234);
    REQUIRE(config.diagnostics.connection.ipv4.executable == "curl");
    REQUIRE(config.diagnostics.connection.dns.executable == "dig");
    REQUIRE(config.diagnostics.connection.location.enabled == true);
    REQUIRE(config.diagnostics.connection.location.databasePath == "/tmp/tunlet-root/geo/GeoLite2-City.mmdb");
    REQUIRE(config.diagnostics.connection.location.downloadUrl == "https://example.test/GeoLite2-City.mmdb");
    REQUIRE(config.editing.createBackup == false);
    REQUIRE(config.tray.keepRunningWithoutWindow == false);
    REQUIRE(config.tray.startHidden == true);
}

TEST_CASE("ConfigLoader derives default GeoLite path from config directory", "[config]") {
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
        << "ruleSets:\n"
        << "  forceProxyPath: force-proxy.json\n"
        << "  forceDirectPath: force-direct.json\n"
        << "  autoProxyPath: auto-proxy.json\n"
        << "  autoDirectPath: auto-direct.json\n"
        << "diagnostics:\n"
        << "  connection:\n"
        << "    ipv4:\n"
        << "      executable: curl\n";
    file.close();

    const auto config = tunlet::config::ConfigLoader::loadFromPath(configPath);
    REQUIRE(config.configRoute == dir.path());
    REQUIRE(config.ruleSets.forceProxyPath == dir.path() + "/force-proxy.json");
    REQUIRE(config.diagnostics.connection.location.databasePath == dir.path() + "/GeoLite2-City.mmdb");
    REQUIRE(config.tray.keepRunningWithoutWindow == true);
    REQUIRE(config.tray.startHidden == false);
}

TEST_CASE("ConfigLoader can disable built-in default profiles", "[config]") {
    const QString config = QString()
        + "clashApi:\n"
        + "  host: 127.0.0.1\n"
        + "  port: 9090\n"
        + "  disableDefaultProfiles: true\n"
        + "  profiles:\n"
        + "    - name: gaming\n"
        + "      mode: gaming\n"
        + "ruleSets:\n"
        + "  forceProxyPath: /tmp/force-proxy.json\n"
        + "  forceDirectPath: /tmp/force-direct.json\n"
        + "  autoProxyPath: /tmp/auto-proxy.json\n"
        + "  autoDirectPath: /tmp/auto-direct.json\n";

    const auto parsed = tunlet::config::ConfigLoader::loadFromData(config, "/tmp/tunlet/config.yaml");
    REQUIRE(parsed.clashApi.disableDefaultProfiles == true);
    REQUIRE(parsed.clashApi.profiles.size() == 1);
    REQUIRE(parsed.clashApi.profiles.at(0).name == "gaming");
}

TEST_CASE("ConfigLoader rejects legacy externalIp diagnostics schema", "[config]") {
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
        << "ruleSets:\n"
        << "  forceProxyPath: /tmp/force-proxy.json\n"
        << "  forceDirectPath: /tmp/force-direct.json\n"
        << "  autoProxyPath: /tmp/auto-proxy.json\n"
        << "  autoDirectPath: /tmp/auto-direct.json\n"
        << "diagnostics:\n"
        << "  externalIp:\n"
        << "    enabled: true\n";
    file.close();

    REQUIRE_THROWS(tunlet::config::ConfigLoader::loadFromPath(configPath));
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
