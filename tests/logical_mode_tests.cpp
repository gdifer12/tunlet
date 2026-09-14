#include "clash/mode_controller.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <catch2/catch_test_macros.hpp>

namespace tunlet::clash {

class StubClashApiClient final : public ClashApiClient {
public:
    StubClashApiClient() : ClashApiClient(1000) {}

    using ClashApiClient::healthCheckFinished;
    using ClashApiClient::modeStateFinished;
    using ClashApiClient::modeSwitchFinished;
    using ClashApiClient::proxySelectorStateFinished;
    using ClashApiClient::proxySwitchFinished;
};

}  // namespace tunlet::clash

namespace {

QCoreApplication *ensureCoreApplication() {
    if (QCoreApplication::instance()) {
        return QCoreApplication::instance();
    }

    static int argc = 1;
    static char appName[] = "tunlet_mode_tests";
    static char *argv[] = {appName, nullptr};
    return new QCoreApplication(argc, argv);
}

}  // namespace

TEST_CASE("ModeController resolves mapped logical mode", "[mode]") {
    tunlet::config::AppConfig config;
    config.clashApi.host = "127.0.0.1";
    config.clashApi.port = 9090;
    config.clashApi.profiles = {
        {"direct", "direct", {}},
        {"proxy", "global", {}},
        {"auto", "rule", {}},
        {"gaming", "gaming", {}},
    };

    tunlet::clash::StubClashApiClient client;
    tunlet::clash::ModeController controller(config, &client);

    emit client.modeStateFinished({
        .ok = true,
        .detail = "ok",
        .currentMode = "gaming",
        .supportedModes = {"direct", "global", "rule", "gaming"},
    });

    const auto status = controller.status();
    REQUIRE(status.currentProfileName == "gaming");
    REQUIRE(status.currentModeValue == "gaming");
    REQUIRE(status.supportedModes.size() == 4);
    REQUIRE(status.reachable);
}

TEST_CASE("ModeController maps Clash rule mode to logical auto profile", "[mode]") {
    tunlet::config::AppConfig config;
    config.clashApi.host = "127.0.0.1";
    config.clashApi.port = 9090;
    config.clashApi.profiles = {
        {"direct", "direct", {}},
        {"proxy", "global", {}},
        {"auto", "rule", {}},
    };

    tunlet::clash::StubClashApiClient client;
    tunlet::clash::ModeController controller(config, &client);

    emit client.modeStateFinished({
        .ok = true,
        .detail = "ok",
        .currentMode = "rule",
        .supportedModes = {"direct", "global", "rule"},
    });

    const auto status = controller.status();
    REQUIRE(status.currentProfileName == "auto");
    REQUIRE(status.currentModeValue == "rule");
    REQUIRE(status.supportedModes.contains("rule"));
}

TEST_CASE("ModeController matches mode values case-insensitively", "[mode]") {
    tunlet::config::AppConfig config;
    config.clashApi.host = "127.0.0.1";
    config.clashApi.port = 9090;
    config.clashApi.profiles = {
        {"direct", "direct", {}},
        {"proxy", "global", {}},
        {"auto", "rule", {}},
    };

    tunlet::clash::StubClashApiClient client;
    tunlet::clash::ModeController controller(config, &client);

    emit client.modeStateFinished({
        .ok = true,
        .detail = "ok",
        .currentMode = "Rule",
        .supportedModes = {"direct", "global", "rule"},
    });

    const auto status = controller.status();
    REQUIRE(status.currentProfileName == "auto");
    REQUIRE(status.currentModeValue == "Rule");
}

TEST_CASE("ModeController exposes supported configured profiles before runtime-only modes", "[mode]") {
    tunlet::config::AppConfig config;
    config.clashApi.host = "127.0.0.1";
    config.clashApi.port = 9090;
    config.clashApi.displayAllModes = true;
    config.clashApi.profiles = {
        {"direct", "direct", {}},
        {"proxy", "global", {}},
        {"auto", "rule", {}},
        {"gaming", "gaming", {}},
    };

    tunlet::clash::StubClashApiClient client;
    tunlet::clash::ModeController controller(config, &client);
    emit client.modeStateFinished({
        .ok = true,
        .detail = "ok",
        .currentMode = "gaming",
        .supportedModes = {"GLOBAL", "gaming", "custom", "proxy"},
    });

    const auto options = controller.profiles();
    REQUIRE(options.size() == 4);
    REQUIRE(options.at(0).name == "proxy");
    REQUIRE(options.at(0).id == "configured:proxy");
    REQUIRE(options.at(1).name == "gaming");
    REQUIRE(options.at(2).name == "custom");
    REQUIRE(options.at(2).id == "runtime:custom");
    REQUIRE(options.at(3).name == "proxy");
    REQUIRE(options.at(3).id == "runtime:proxy");
}

TEST_CASE("ModeController can hide runtime-only modes", "[mode]") {
    tunlet::config::AppConfig config;
    config.clashApi.host = "127.0.0.1";
    config.clashApi.port = 9090;
    config.clashApi.displayAllModes = false;
    config.clashApi.profiles = {
        {"direct", "direct", {}},
        {"work", "work", {}},
    };

    tunlet::clash::StubClashApiClient client;
    tunlet::clash::ModeController controller(config, &client);
    emit client.modeStateFinished({
        .ok = true,
        .detail = "ok",
        .currentMode = "extra",
        .supportedModes = {"work", "extra"},
    });

    const auto options = controller.profiles();
    REQUIRE(options.size() == 1);
    REQUIRE(options.at(0).name == "work");
    REQUIRE(controller.status().currentProfileName == "unknown");
}

TEST_CASE("ModeController only announces mode options when the runtime list changes", "[mode]") {
    tunlet::config::AppConfig config;
    config.clashApi.host = "127.0.0.1";
    config.clashApi.port = 9090;
    config.clashApi.profiles = {{"direct", "direct", {}}};

    tunlet::clash::StubClashApiClient client;
    tunlet::clash::ModeController controller(config, &client);
    int profileUpdates = 0;
    QObject::connect(&controller,
                     &tunlet::clash::ModeController::profilesUpdated,
                     &controller,
                     [&](const auto &) { ++profileUpdates; });

    const tunlet::clash::ModeStateResult state = {
        .ok = true,
        .detail = "ok",
        .currentMode = "direct",
        .supportedModes = {"direct", "custom"},
    };
    emit client.modeStateFinished(state);
    emit client.modeStateFinished(state);

    REQUIRE(profileUpdates == 1);
}

TEST_CASE("ModeController keeps last-known proxy state stale after refresh failure", "[mode][proxy]") {
    tunlet::config::AppConfig config;
    config.clashApi.host = "127.0.0.1";
    config.clashApi.port = 9090;
    config.clashApi.editProxySelector = true;
    config.clashApi.proxySelector = "proxy";

    tunlet::clash::StubClashApiClient client;
    tunlet::clash::ModeController controller(config, &client);
    emit client.proxySelectorStateFinished({
        .ok = true,
        .detail = "ok",
        .selectorName = "proxy",
        .currentProxy = "node-a",
        .availableProxies = {"node-a", "node-b"},
    });

    REQUIRE(controller.status().proxySelector.reachable);
    REQUIRE(controller.status().proxySelector.currentProxy == "node-a");
    REQUIRE_FALSE(controller.status().proxySelector.stale);

    emit client.proxySelectorStateFinished({
        .ok = false,
        .detail = "connection refused",
        .selectorName = "proxy",
    });

    REQUIRE_FALSE(controller.status().proxySelector.reachable);
    REQUIRE(controller.status().proxySelector.stale);
    REQUIRE(controller.status().proxySelector.currentProxy == "node-a");
    REQUIRE(controller.status().proxySelector.availableProxies == QStringList{"node-a", "node-b"});
}

TEST_CASE("ModeController applies proxy selector disable live", "[mode][proxy]") {
    tunlet::config::AppConfig config;
    config.clashApi.host = "127.0.0.1";
    config.clashApi.port = 9090;
    config.clashApi.editProxySelector = true;

    tunlet::clash::StubClashApiClient client;
    tunlet::clash::ModeController controller(config, &client);
    tunlet::config::AppConfig disabled = config;
    disabled.clashApi.editProxySelector = false;
    controller.updateConfig(disabled);

    REQUIRE_FALSE(controller.status().proxySelector.enabled);
    REQUIRE(controller.status().proxySelector.currentProxy.isEmpty());
    REQUIRE(controller.status().proxySelector.availableProxies.isEmpty());
}

TEST_CASE("ModeController polls status periodically when mode sync is enabled", "[mode]") {
    ensureCoreApplication();

    tunlet::config::AppConfig config;
    config.clashApi.host = "127.0.0.1";
    config.clashApi.port = 9090;
    config.clashApi.modeSyncIntervalMs = 20;
    config.clashApi.profiles = {
        {"direct", "direct", {}},
        {"proxy", "global", {}},
        {"auto", "rule", {}},
    };

    tunlet::clash::StubClashApiClient client;
    tunlet::clash::ModeController controller(config, &client);

    int updates = 0;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&controller, &tunlet::clash::ModeController::statusUpdated, &loop, [&](const tunlet::clash::ModeStatus &) {
        ++updates;
        loop.quit();
    });
    timeout.start(200);
    loop.exec();

    REQUIRE(updates > 0);
}

TEST_CASE("ModeController can disable mode sync polling before the first tick", "[mode]") {
    ensureCoreApplication();

    tunlet::config::AppConfig config;
    config.clashApi.host = "127.0.0.1";
    config.clashApi.port = 9090;
    config.clashApi.modeSyncIntervalMs = 30;
    config.clashApi.profiles = {
        {"direct", "direct", {}},
        {"proxy", "global", {}},
        {"auto", "rule", {}},
    };

    tunlet::clash::StubClashApiClient client;
    tunlet::clash::ModeController controller(config, &client);

    tunlet::config::AppConfig disabledConfig = config;
    disabledConfig.clashApi.modeSyncIntervalMs = 0;
    controller.updateConfig(disabledConfig);

    int updates = 0;
    QObject::connect(&controller, &tunlet::clash::ModeController::statusUpdated, &controller, [&](const tunlet::clash::ModeStatus &) {
        ++updates;
    });
    updates = 0;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(100);
    loop.exec();

    REQUIRE(updates == 0);
}
