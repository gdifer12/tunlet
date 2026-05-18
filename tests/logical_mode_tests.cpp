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
