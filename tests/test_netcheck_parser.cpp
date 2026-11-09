/**
 * TailZero - netcheck parser tests (JSON + text fallback).
 * SPDX-License-Identifier: MIT
 */
#include <string>

#include "tailscale/status_parser.h"
#include "test_harness.h"

using namespace tailzero;

TZ_TEST(netcheck_json_from_device) {
    const std::string text = tztest::fixture_text("connected/netcheck.json");
    TZ_REQUIRE(!text.empty());
    const NetcheckReport report = parse_netcheck_json(text);
    TZ_CHECK(report.parsed);
    TZ_CHECK(report.error.empty());
    TZ_CHECK(report.udp);
    TZ_CHECK(report.ipv4);
    TZ_CHECK(report.ipv6);
    TZ_CHECK(!report.mapping_varies_by_dest_ip);
    TZ_CHECK(!report.hair_pinning);
    TZ_CHECK_EQ(report.port_mapping, std::string("UPnP, NAT-PMP, PCP"));
    TZ_CHECK_EQ(report.ipv4_addr, std::string("203.0.113.45:29126"));
    TZ_CHECK_EQ(report.ipv6_addr, std::string("[2001:db8::1]:56885"));
    TZ_CHECK_EQ(report.preferred_derp_id, 20);
    TZ_CHECK_EQ(report.region_latency_ms.size(), std::size_t(26));
    // 58521206 ns -> 59 ms (Go durations are nanoseconds).
    TZ_CHECK_EQ(tztest::map_value(report.region_latency_ms, "20"), 59);
    TZ_CHECK_EQ(tztest::map_value(report.region_latency_ms, "3"), 94);
}

TZ_TEST(netcheck_json_missing_fields) {
    const NetcheckReport report = parse_netcheck_json(R"({"UDP": true})");
    TZ_CHECK(report.parsed);
    TZ_CHECK(report.udp);
    TZ_CHECK(!report.ipv4);
    TZ_CHECK(!report.ipv6);
    TZ_CHECK_EQ(report.preferred_derp_id, -1);
    TZ_CHECK(report.region_latency_ms.empty());
    TZ_CHECK_EQ(report.port_mapping, std::string("none"));
}

TZ_TEST(netcheck_json_invalid) {
    const NetcheckReport report = parse_netcheck_json("{\"UDP\": }");
    TZ_CHECK(!report.parsed);
    TZ_CHECK(report.error.find("netcheck JSON") != std::string::npos);
    const NetcheckReport scalar = parse_netcheck_json("42");
    TZ_CHECK(!scalar.parsed);
}

TZ_TEST(netcheck_text_from_device) {
    const std::string text = tztest::fixture_text("raw/netcheck-text.txt");
    TZ_REQUIRE(!text.empty());
    const NetcheckReport report = parse_netcheck_text(text);
    TZ_CHECK(report.parsed);
    TZ_CHECK(report.error.empty());
    TZ_CHECK(report.udp);
    TZ_CHECK(report.ipv4);
    TZ_CHECK(report.ipv6);
    TZ_CHECK_EQ(report.ipv4_addr, std::string("203.0.113.45:54039"));
    TZ_CHECK_EQ(report.ipv6_addr, std::string("[2001:db8::1]:53588"));
    TZ_CHECK(!report.mapping_varies_by_dest_ip);
    TZ_CHECK_EQ(report.port_mapping, std::string("UPnP, NAT-PMP, PCP"));
    TZ_CHECK_EQ(report.preferred_derp, std::string("hkg"));
    TZ_CHECK_EQ(report.preferred_derp_city, std::string("Hong Kong"));
    TZ_CHECK_EQ(tztest::map_value(report.region_latency_ms, "hkg"), 48);
    TZ_CHECK_EQ(tztest::map_value(report.region_latency_ms, "par"), 217);
    TZ_CHECK_EQ(tztest::map_value(report.region_latency_ms, "sao"), 339);
    TZ_CHECK_EQ(tztest::map_value(report.region_names, "hkg"), std::string("Hong Kong"));
    TZ_CHECK_EQ(tztest::map_value(report.region_names, "bom"), std::string("Mumbai"));
    // Two regions have no measured latency (bom, hnl); they still carry names.
    TZ_CHECK_EQ(report.region_latency_ms.size(), std::size_t(26));
}

TZ_TEST(netcheck_text_no_ipv4) {
    const std::string text =
        "Report:\n"
        "\t* UDP: false\n"
        "\t* IPv4: no\n"
        "\t* IPv6: no\n"
        "\t* PortMapping: none\n";
    const NetcheckReport report = parse_netcheck_text(text);
    TZ_CHECK(report.parsed);
    TZ_CHECK(!report.udp);
    TZ_CHECK(!report.ipv4);
    TZ_CHECK(!report.ipv6);
    TZ_CHECK_EQ(report.port_mapping, std::string("none"));
}

TZ_TEST(netcheck_text_unrecognized) {
    const NetcheckReport report = parse_netcheck_text("hello world\n");
    TZ_CHECK(!report.parsed);
    TZ_CHECK(!report.error.empty());
}
