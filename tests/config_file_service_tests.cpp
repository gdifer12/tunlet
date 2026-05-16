#include "config/config_file_service.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ConfigFileService reports YAML parse coordinates", "[config]") {
    tunlet::config::ConfigFileService service({});

    const auto result = service.validateConfigText(
        "/tmp/config.yaml",
        "clashApi:\n"
        "  host: 127.0.0.1\n"
        "  port: [\n"
        "ruleSets:\n"
        "  forceProxyPath: one.json\n"
        "  forceDirectPath: two.json\n"
        "  autoProxyPath: three.json\n"
        "  autoDirectPath: four.json\n");

    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.error.isEmpty());
    REQUIRE(result.errorLine >= 1);
    REQUIRE(result.errorColumn >= 1);
}
