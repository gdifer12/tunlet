#include "ui/mode_selection_state.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Mode selection stays empty until controller knows the active profile", "[ui][mode]") {
    const QVector<tunlet::clash::ModeOption> profiles = {
        {"configured:direct", "direct", "direct", {}},
        {"configured:proxy", "proxy", "global", {}},
        {"configured:auto", "auto", "rule", {}},
    };
    tunlet::clash::ModeStatus status;

    REQUIRE(tunlet::ui::syncModeProfileSelection(profiles, status, {}) .isEmpty());
}

TEST_CASE("Mode selection adopts the resolved active profile", "[ui][mode]") {
    const QVector<tunlet::clash::ModeOption> profiles = {
        {"configured:direct", "direct", "direct", {}},
        {"configured:proxy", "proxy", "global", {}},
        {"configured:auto", "auto", "rule", {}},
    };
    tunlet::clash::ModeStatus status;
    status.currentProfileId = "configured:proxy";
    status.currentProfileName = "proxy";

    REQUIRE(tunlet::ui::syncModeProfileSelection(profiles, status, "configured:direct") == "configured:proxy");
    REQUIRE(tunlet::ui::syncModeProfileSelection(profiles, status, {}) == "configured:proxy");
}

TEST_CASE("Mode selection preserves a pending user target while controller is busy", "[ui][mode]") {
    const QVector<tunlet::clash::ModeOption> profiles = {
        {"configured:direct", "direct", "direct", {}},
        {"configured:proxy", "proxy", "global", {}},
        {"configured:auto", "auto", "rule", {}},
    };
    tunlet::clash::ModeStatus status;
    status.currentProfileId = "configured:direct";
    status.currentProfileName = "direct";
    status.busy = true;

    REQUIRE(tunlet::ui::syncModeProfileSelection(profiles, status, "configured:proxy") == "configured:proxy");
}

TEST_CASE("Mode selection clears unknown controller states instead of claiming the first profile", "[ui][mode]") {
    const QVector<tunlet::clash::ModeOption> profiles = {
        {"configured:direct", "direct", "direct", {}},
        {"configured:proxy", "proxy", "global", {}},
        {"configured:auto", "auto", "rule", {}},
    };
    tunlet::clash::ModeStatus status;
    status.currentProfileName = "unknown";
    status.currentModeValue = "gaming";

    REQUIRE(tunlet::ui::syncModeProfileSelection(profiles, status, "configured:direct").isEmpty());
}
