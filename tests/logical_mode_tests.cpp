#include "clash/mode_controller.hpp"

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
