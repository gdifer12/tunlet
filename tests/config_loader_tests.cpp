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
        << "  modeSyncIntervalMs: 15000\n"
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
        << "      mode: dynamic_cache\n"
        << "      localDb:\n"
        << "        databasePath: geo/GeoLite2-City.mmdb\n"
        << "        asnDatabasePath: geo/GeoLite2-ASN.mmdb\n"
        << "        downloadUrl: https://example.test/GeoLite2-City.mmdb\n"
        << "      dynamicCache:\n"
        << "        provider: ipwhois\n"
        << "        url: https://ipwho.is/\n"
        << "        cachePath: geo/geoip-cache.json\n"
        << "        baseRefreshDays: 14\n"
        << "        randomShiftDays: 3\n"
        << "        timeoutMs: 5000\n"
        << "        refreshOnStartup: false\n"
        << "        allowManualRefresh: true\n"
        << "editing:\n"
        << "  createBackup: false\n"
        << "theme:\n"
        << "  themePath: themes/custom.theme.json\n"
        << "  templatePath: themes/custom.qss.in\n"
        << "  qssPath: themes/overlay.qss\n"
        << "tray:\n"
        << "  keepRunningWithoutWindow: false\n"
        << "  startHidden: true\n"
        << "  interactiveRefreshIntervalMs: 7000\n"
        << "logging:\n"
        << "  enabled: true\n"
        << "  level: warning\n"
        << "  textPath: logs/tunlet.log\n"
        << "  jsonlPath: logs/tunlet.jsonl\n"
        << "  rotation:\n"
        << "    enabled: true\n"
        << "    maxFileBytes: 4096\n"
        << "    keepFiles: 3\n"
        << "ui:\n"
        << "  textSelection:\n"
        << "    enableInformationalLabels: false\n"
        << "  keyboard:\n"
        << "    shortcuts:\n"
        << "      closeWindowPrimary: Ctrl+Q\n"
        << "      closeWindowSecondary: \"\"\n"
        << "      nextPage: Alt+Right\n"
        << "      previousPage: Alt+Left\n"
        << "      pageMain: Alt+1\n"
        << "      pageRules: Alt+2\n"
        << "      pageSettings: Alt+3\n"
        << "      openModeSelector: Alt+M\n"
        << "      openRuleFileSelector: Alt+R\n"
        << "      refreshRuntime: F6\n"
        << "      refreshLocationData: Ctrl+F6\n"
        << "      validateEditor: Ctrl+Alt+V\n"
        << "      saveEditor: Ctrl+Shift+S\n"
        << "      reloadEditor: Ctrl+Alt+R\n";
    file.close();

    const auto config = tunlet::config::ConfigLoader::loadFromPath(configPath);
    REQUIRE(config.clashApi.host == "127.0.0.1");
    REQUIRE(config.clashApi.port == 9090);
    REQUIRE(config.clashApi.modeSyncIntervalMs == 15000);
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
    REQUIRE(config.diagnostics.connection.location.mode == tunlet::config::DiagnosticsLocationMode::DynamicCache);
    REQUIRE(config.diagnostics.connection.location.localDb.databasePath == "/tmp/tunlet-root/geo/GeoLite2-City.mmdb");
    REQUIRE(config.diagnostics.connection.location.localDb.asnDatabasePath == "/tmp/tunlet-root/geo/GeoLite2-ASN.mmdb");
    REQUIRE(config.diagnostics.connection.location.localDb.downloadUrl == "https://example.test/GeoLite2-City.mmdb");
    REQUIRE(config.diagnostics.connection.location.dynamicCache.provider == "ipwhois");
    REQUIRE(config.diagnostics.connection.location.dynamicCache.url == "https://ipwho.is/");
    REQUIRE(config.diagnostics.connection.location.dynamicCache.cachePath == "/tmp/tunlet-root/geo/geoip-cache.json");
    REQUIRE(config.diagnostics.connection.location.dynamicCache.baseRefreshDays == 14);
    REQUIRE(config.diagnostics.connection.location.dynamicCache.randomShiftDays == 3);
    REQUIRE(config.diagnostics.connection.location.dynamicCache.timeoutMs == 5000);
    REQUIRE(config.diagnostics.connection.location.dynamicCache.refreshOnStartup == false);
    REQUIRE(config.diagnostics.connection.location.dynamicCache.allowManualRefresh == true);
    REQUIRE(config.editing.createBackup == false);
    REQUIRE(config.theme.themePath == "/tmp/tunlet-root/themes/custom.theme.json");
    REQUIRE(config.theme.templatePath == "/tmp/tunlet-root/themes/custom.qss.in");
    REQUIRE(config.theme.qssPath == "/tmp/tunlet-root/themes/overlay.qss");
    REQUIRE(config.tray.keepRunningWithoutWindow == false);
    REQUIRE(config.tray.startHidden == true);
    REQUIRE(config.tray.interactiveRefreshIntervalMs == 7000);
    REQUIRE(config.logging.enabled == true);
    REQUIRE(config.logging.level == tunlet::config::LoggingLevel::Warning);
    REQUIRE(config.logging.textPath == "/tmp/tunlet-root/logs/tunlet.log");
    REQUIRE(config.logging.jsonlPath == "/tmp/tunlet-root/logs/tunlet.jsonl");
    REQUIRE(config.logging.rotation.enabled == true);
    REQUIRE(config.logging.rotation.maxFileBytes == 4096);
    REQUIRE(config.logging.rotation.keepFiles == 3);
    REQUIRE(config.ui.textSelection.enableInformationalLabels == false);
    REQUIRE(config.ui.keyboard.shortcuts.closeWindowPrimary == "Ctrl+Q");
    REQUIRE(config.ui.keyboard.shortcuts.closeWindowSecondary.isEmpty());
    REQUIRE(config.ui.keyboard.shortcuts.nextPage == "Alt+Right");
    REQUIRE(config.ui.keyboard.shortcuts.previousPage == "Alt+Left");
    REQUIRE(config.ui.keyboard.shortcuts.pageMain == "Alt+1");
    REQUIRE(config.ui.keyboard.shortcuts.pageRules == "Alt+2");
    REQUIRE(config.ui.keyboard.shortcuts.pageSettings == "Alt+3");
    REQUIRE(config.ui.keyboard.shortcuts.openModeSelector == "Alt+M");
    REQUIRE(config.ui.keyboard.shortcuts.openRuleFileSelector == "Alt+R");
    REQUIRE(config.ui.keyboard.shortcuts.refreshRuntime == "F6");
    REQUIRE(config.ui.keyboard.shortcuts.refreshLocationData == "Ctrl+F6");
    REQUIRE(config.ui.keyboard.shortcuts.validateEditor == "Ctrl+Alt+V");
    REQUIRE(config.ui.keyboard.shortcuts.saveEditor == "Ctrl+Shift+S");
    REQUIRE(config.ui.keyboard.shortcuts.reloadEditor == "Ctrl+Alt+R");
}

TEST_CASE("ConfigLoader derives default GeoIP paths from config directory", "[config]") {
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
    REQUIRE(config.clashApi.modeSyncIntervalMs == 0);
    REQUIRE(config.ruleSets.forceProxyPath == dir.path() + "/force-proxy.json");
    REQUIRE(config.diagnostics.connection.location.mode == tunlet::config::DiagnosticsLocationMode::LocalDb);
    REQUIRE(config.diagnostics.connection.location.localDb.databasePath == dir.path() + "/GeoLite2-City.mmdb");
    REQUIRE(config.diagnostics.connection.location.localDb.asnDatabasePath == dir.path() + "/GeoLite2-ASN.mmdb");
    REQUIRE(config.diagnostics.connection.location.dynamicCache.cachePath == dir.path() + "/geoip-cache.json");
    REQUIRE(config.tray.keepRunningWithoutWindow == true);
    REQUIRE(config.tray.startHidden == false);
    REQUIRE(config.tray.interactiveRefreshIntervalMs == 5000);
    REQUIRE(config.theme.themePath.isEmpty());
    REQUIRE(config.theme.templatePath.isEmpty());
    REQUIRE(config.theme.qssPath.isEmpty());
    REQUIRE(config.logging.enabled == false);
    REQUIRE(config.logging.level == tunlet::config::LoggingLevel::Info);
    REQUIRE(config.logging.textPath.isEmpty());
    REQUIRE(config.logging.jsonlPath.isEmpty());
    REQUIRE(config.logging.rotation.enabled == false);
    REQUIRE(config.logging.rotation.maxFileBytes == 0);
    REQUIRE(config.logging.rotation.keepFiles == 0);
    REQUIRE(config.ui.textSelection.enableInformationalLabels == true);
    REQUIRE(config.ui.keyboard.shortcuts.closeWindowPrimary == "Esc");
    REQUIRE(config.ui.keyboard.shortcuts.closeWindowSecondary == "Q");
    REQUIRE(config.ui.keyboard.shortcuts.nextPage == "Ctrl+Tab");
    REQUIRE(config.ui.keyboard.shortcuts.previousPage == "Ctrl+Shift+Tab");
    REQUIRE(config.ui.keyboard.shortcuts.pageMain == "1");
    REQUIRE(config.ui.keyboard.shortcuts.pageRules == "2");
    REQUIRE(config.ui.keyboard.shortcuts.pageSettings == "3");
    REQUIRE(config.ui.keyboard.shortcuts.openModeSelector == "M");
    REQUIRE(config.ui.keyboard.shortcuts.openRuleFileSelector == "R");
    REQUIRE(config.ui.keyboard.shortcuts.refreshRuntime == "F5");
    REQUIRE(config.ui.keyboard.shortcuts.refreshLocationData == "Shift+F5");
    REQUIRE(config.ui.keyboard.shortcuts.validateEditor == "Ctrl+Shift+V");
    REQUIRE(config.ui.keyboard.shortcuts.saveEditor == "Ctrl+S");
    REQUIRE(config.ui.keyboard.shortcuts.reloadEditor == "Ctrl+R");
}

TEST_CASE("ConfigLoader resolves theme override paths through configRoute", "[config]") {
    const QString config = QString()
        + "configRoute: /tmp/tunlet-root\n"
        + "clashApi:\n"
        + "  host: 127.0.0.1\n"
        + "  port: 9090\n"
        + "ruleSets:\n"
        + "  forceProxyPath: force-proxy.json\n"
        + "  forceDirectPath: force-direct.json\n"
        + "  autoProxyPath: auto-proxy.json\n"
        + "  autoDirectPath: auto-direct.json\n"
        + "theme:\n"
        + "  themePath: themes/custom.theme.json\n"
        + "  templatePath: themes/custom.qss.in\n"
        + "  qssPath: themes/overlay.qss\n";

    const auto parsed = tunlet::config::ConfigLoader::loadFromData(config, "/tmp/tunlet-root/config.yaml");
    REQUIRE(parsed.theme.themePath == "/tmp/tunlet-root/themes/custom.theme.json");
    REQUIRE(parsed.theme.templatePath == "/tmp/tunlet-root/themes/custom.qss.in");
    REQUIRE(parsed.theme.qssPath == "/tmp/tunlet-root/themes/overlay.qss");
}

TEST_CASE("ConfigLoader supports legacy location enabled/databasePath fields", "[config]") {
    const QString config = QString()
        + "clashApi:\n"
        + "  host: 127.0.0.1\n"
        + "  port: 9090\n"
        + "ruleSets:\n"
        + "  forceProxyPath: /tmp/force-proxy.json\n"
        + "  forceDirectPath: /tmp/force-direct.json\n"
        + "  autoProxyPath: /tmp/auto-proxy.json\n"
        + "  autoDirectPath: /tmp/auto-direct.json\n"
        + "diagnostics:\n"
        + "  connection:\n"
        + "    location:\n"
        + "      enabled: false\n"
        + "      databasePath: /tmp/GeoLite2-City.mmdb\n";

    const auto parsed = tunlet::config::ConfigLoader::loadFromData(config, "/tmp/tunlet/config.yaml");
    REQUIRE(parsed.diagnostics.connection.location.mode == tunlet::config::DiagnosticsLocationMode::Disabled);
    REQUIRE(parsed.diagnostics.connection.location.localDb.databasePath == "/tmp/GeoLite2-City.mmdb");
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

TEST_CASE("ConfigLoader rejects negative mode sync interval", "[config]") {
    const QString config = QString()
        + "clashApi:\n"
        + "  host: 127.0.0.1\n"
        + "  port: 9090\n"
        + "  modeSyncIntervalMs: -1\n"
        + "ruleSets:\n"
        + "  forceProxyPath: /tmp/force-proxy.json\n"
        + "  forceDirectPath: /tmp/force-direct.json\n"
        + "  autoProxyPath: /tmp/auto-proxy.json\n"
        + "  autoDirectPath: /tmp/auto-direct.json\n";

    REQUIRE_THROWS(tunlet::config::ConfigLoader::loadFromData(config, "/tmp/tunlet/config.yaml"));
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

TEST_CASE("ConfigLoader rejects non-positive tray interactive refresh interval", "[config]") {
    const QString config = QString()
        + "clashApi:\n"
        + "  host: 127.0.0.1\n"
        + "  port: 9090\n"
        + "ruleSets:\n"
        + "  forceProxyPath: /tmp/force-proxy.json\n"
        + "  forceDirectPath: /tmp/force-direct.json\n"
        + "  autoProxyPath: /tmp/auto-proxy.json\n"
        + "  autoDirectPath: /tmp/auto-direct.json\n"
        + "tray:\n"
        + "  interactiveRefreshIntervalMs: 0\n";

    REQUIRE_THROWS(tunlet::config::ConfigLoader::loadFromData(config, "/tmp/tunlet/config.yaml"));
}

TEST_CASE("ConfigLoader rejects unsupported logging level", "[config]") {
    const QString config = QString()
        + "clashApi:\n"
        + "  host: 127.0.0.1\n"
        + "  port: 9090\n"
        + "ruleSets:\n"
        + "  forceProxyPath: /tmp/force-proxy.json\n"
        + "  forceDirectPath: /tmp/force-direct.json\n"
        + "  autoProxyPath: /tmp/auto-proxy.json\n"
        + "  autoDirectPath: /tmp/auto-direct.json\n"
        + "logging:\n"
        + "  enabled: true\n"
        + "  level: verbose\n"
        + "  textPath: /tmp/tunlet.log\n";

    REQUIRE_THROWS(tunlet::config::ConfigLoader::loadFromData(config, "/tmp/tunlet/config.yaml"));
}

TEST_CASE("ConfigLoader requires a sink path when logging is enabled", "[config]") {
    const QString config = QString()
        + "clashApi:\n"
        + "  host: 127.0.0.1\n"
        + "  port: 9090\n"
        + "ruleSets:\n"
        + "  forceProxyPath: /tmp/force-proxy.json\n"
        + "  forceDirectPath: /tmp/force-direct.json\n"
        + "  autoProxyPath: /tmp/auto-proxy.json\n"
        + "  autoDirectPath: /tmp/auto-direct.json\n"
        + "logging:\n"
        + "  enabled: true\n";

    REQUIRE_THROWS(tunlet::config::ConfigLoader::loadFromData(config, "/tmp/tunlet/config.yaml"));
}

TEST_CASE("ConfigLoader rejects identical logging sink paths", "[config]") {
    const QString config = QString()
        + "clashApi:\n"
        + "  host: 127.0.0.1\n"
        + "  port: 9090\n"
        + "ruleSets:\n"
        + "  forceProxyPath: /tmp/force-proxy.json\n"
        + "  forceDirectPath: /tmp/force-direct.json\n"
        + "  autoProxyPath: /tmp/auto-proxy.json\n"
        + "  autoDirectPath: /tmp/auto-direct.json\n"
        + "logging:\n"
        + "  enabled: true\n"
        + "  textPath: /tmp/tunlet.log\n"
        + "  jsonlPath: /tmp/tunlet.log\n";

    REQUIRE_THROWS(tunlet::config::ConfigLoader::loadFromData(config, "/tmp/tunlet/config.yaml"));
}

TEST_CASE("ConfigLoader rejects non-positive logging rotation size", "[config]") {
    const QString config = QString()
        + "clashApi:\n"
        + "  host: 127.0.0.1\n"
        + "  port: 9090\n"
        + "ruleSets:\n"
        + "  forceProxyPath: /tmp/force-proxy.json\n"
        + "  forceDirectPath: /tmp/force-direct.json\n"
        + "  autoProxyPath: /tmp/auto-proxy.json\n"
        + "  autoDirectPath: /tmp/auto-direct.json\n"
        + "logging:\n"
        + "  enabled: true\n"
        + "  textPath: /tmp/tunlet.log\n"
        + "  rotation:\n"
        + "    enabled: true\n"
        + "    maxFileBytes: 0\n"
        + "    keepFiles: 2\n";

    REQUIRE_THROWS(tunlet::config::ConfigLoader::loadFromData(config, "/tmp/tunlet/config.yaml"));
}

TEST_CASE("ConfigLoader rejects invalid logging rotation retention", "[config]") {
    const QString config = QString()
        + "clashApi:\n"
        + "  host: 127.0.0.1\n"
        + "  port: 9090\n"
        + "ruleSets:\n"
        + "  forceProxyPath: /tmp/force-proxy.json\n"
        + "  forceDirectPath: /tmp/force-direct.json\n"
        + "  autoProxyPath: /tmp/auto-proxy.json\n"
        + "  autoDirectPath: /tmp/auto-direct.json\n"
        + "logging:\n"
        + "  enabled: true\n"
        + "  textPath: /tmp/tunlet.log\n"
        + "  rotation:\n"
        + "    enabled: true\n"
        + "    maxFileBytes: 1024\n"
        + "    keepFiles: 0\n";

    REQUIRE_THROWS(tunlet::config::ConfigLoader::loadFromData(config, "/tmp/tunlet/config.yaml"));
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
