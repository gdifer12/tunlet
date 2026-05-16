#include "diagnostics/diagnostics_parsing.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("parsePublicIpOutput extracts plain and JSON IP responses", "[diagnostics]") {
    QString failureReason;
    REQUIRE(tunlet::diagnostics::parsePublicIpOutput("198.51.100.10\n", &failureReason) == "198.51.100.10");
    REQUIRE(tunlet::diagnostics::parsePublicIpOutput(R"({"ip":"203.0.113.20"})", &failureReason) == "203.0.113.20");
}

TEST_CASE("parseTimingOutput reads curl write-out format", "[diagnostics]") {
    QString failureReason;
    const auto parsed = tunlet::diagnostics::parseTimingOutput(
        "dns=0.012s connect=0.034s tls=0.101s total=0.220s\n",
        &failureReason);

    REQUIRE(parsed.ok);
    REQUIRE(parsed.dnsMs == 12);
    REQUIRE(parsed.connectMs == 34);
    REQUIRE(parsed.tlsMs == 101);
    REQUIRE(parsed.totalMs == 220);
}

TEST_CASE("parseDnsOutput cleans dig TXT responses", "[diagnostics]") {
    QString failureReason;
    const QString parsed =
        tunlet::diagnostics::parseDnsOutput("\"ns1.google.com\"\n\"edns0-client-subnet 0.0.0.0/0\"\n", &failureReason);

    REQUIRE(parsed == "ns1.google.com | edns0-client-subnet 0.0.0.0/0");
}
