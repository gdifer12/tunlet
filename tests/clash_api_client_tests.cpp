#include "clash/clash_api_client.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ClashApiClient normalizes built-in mode names for writes", "[clash]") {
    REQUIRE(tunlet::clash::toClashWriteMode("direct") == "Direct");
    REQUIRE(tunlet::clash::toClashWriteMode("global") == "Global");
    REQUIRE(tunlet::clash::toClashWriteMode("rule") == "Rule");
}

TEST_CASE("ClashApiClient preserves custom mode names for writes", "[clash]") {
    REQUIRE(tunlet::clash::toClashWriteMode("gaming") == "gaming");
    REQUIRE(tunlet::clash::toClashWriteMode("WorkMode") == "WorkMode");
}
