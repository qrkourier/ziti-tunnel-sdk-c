/*
 Copyright NetFoundry Inc.

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 https://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

#include "catch2/catch.hpp"
extern "C" {
#include "ziti/ziti_tunnel.h"
#include "ziti_tunnel_priv.h"
}

/** make valid json from a plain string and parse it as a ziti_address */
#define ZA_INIT_STR(za, s) (( parse_ziti_address((za), "\"" s "\"", strlen("\"" s "\"")) ), (za))

TEST_CASE("address_match", "[address]") {
    struct tunneler_ctx_s tctx = { };
    ziti_address za;
    ip_addr_t ip;
    LIST_INIT(&tctx.intercepts);

    intercept_ctx_t *intercept_s1 = intercept_ctx_new(&tctx, "s1", nullptr);
    LIST_INSERT_HEAD(&tctx.intercepts, intercept_s1, entries);
    intercept_ctx_add_address(intercept_s1, ZA_INIT_STR(&za, "192.168.0.88"));
    intercept_ctx_add_protocol(intercept_s1, "tcp");
    intercept_ctx_add_port_range(intercept_s1, 80, 80);

    IP_ADDR4(&ip, 127, 0, 0, 1);
    REQUIRE(lookup_intercept_by_address(&tctx, "tcp", &ip, &ip, 80) == nullptr);

    IP_ADDR4(&ip, 192, 168, 0, 88);
    REQUIRE(lookup_intercept_by_address(&tctx, "tcp", &ip, &ip, 80) == intercept_s1);

    intercept_ctx_t *intercept_s2 = intercept_ctx_new(&tctx, "s2", nullptr);
    LIST_INSERT_HEAD(&tctx.intercepts, intercept_s2, entries);
    intercept_ctx_add_address(intercept_s2, ZA_INIT_STR(&za, "192.168.0.0/24"));
    intercept_ctx_add_protocol(intercept_s2, "tcp");
    intercept_ctx_add_port_range(intercept_s2, 80, 80);

    // s2 should be overlooked even though it matches and precedes s1 in the intercept list
    REQUIRE(lookup_intercept_by_address(&tctx, "tcp", &ip, &ip, 80) == intercept_s1);

    // s2 should match CIDR address
    IP_ADDR4(&ip, 192, 168, 0, 10);
    REQUIRE(lookup_intercept_by_address(&tctx, "tcp", &ip, &ip, 80) == intercept_s2);

    intercept_ctx_t *intercept_s3 = intercept_ctx_new(&tctx, "s3", nullptr);
    LIST_INSERT_HEAD(&tctx.intercepts, intercept_s3, entries);
    intercept_ctx_add_address(intercept_s3, ZA_INIT_STR(&za, "192.168.0.0/16"));
    intercept_ctx_add_protocol(intercept_s3, "tcp");
    intercept_ctx_add_port_range(intercept_s3, 80, 85);

    // s2 should still win due to smaller cidr range
    IP_ADDR4(&ip, 192, 168, 0, 10);
    REQUIRE(lookup_intercept_by_address(&tctx, "tcp", &ip, &ip, 80) == intercept_s2);

    intercept_ctx_t *intercept_s4 = intercept_ctx_new(&tctx, "s4", nullptr);
    LIST_INSERT_HEAD(&tctx.intercepts, intercept_s4, entries);
    intercept_ctx_add_address(intercept_s4, ZA_INIT_STR(&za, "192.168.0.0/16"));
    intercept_ctx_add_protocol(intercept_s4, "tcp");
    intercept_ctx_add_port_range(intercept_s4, 80, 90);

    // s2 should be overlooked despite CIDR match with smaller prefix due to port mismatch
    // s3 should win over s4 due to smaller port range
    IP_ADDR4(&ip, 192, 168, 0, 10);
    REQUIRE(lookup_intercept_by_address(&tctx, "tcp", &ip, &ip, 81) == intercept_s3);

    // s3 should win due to smaller port range with matching cidr
    // s1 should be overlooked despite IP match due to port mismatch
    // s4 should be overlooked due to larger port range
    IP_ADDR4(&ip, 192, 168, 0, 88);
    REQUIRE(lookup_intercept_by_address(&tctx, "tcp", &ip, &ip, 83) == intercept_s3);

    // test source IP whitelist
    ip_addr_t src_allowed;
    ip_addr_t src_denied;
    IP_ADDR4(&src_allowed, 10, 0, 10, 1);
    IP_ADDR4(&src_denied, 10, 0, 10, 2);
    intercept_ctx_add_allowed_source_address(intercept_s3, ZA_INIT_STR(&za, "10.0.10.1"));
    REQUIRE(lookup_intercept_by_address(&tctx, "tcp", &src_allowed, &ip, 83) == intercept_s3);
    // s4 has a larger port range than s3, but no source ip whitelist
    REQUIRE(lookup_intercept_by_address(&tctx, "tcp", &src_denied, &ip, 83) == intercept_s4);

    // verify the intercept cache is populated
    REQUIRE(model_map_get(&tctx.intercepts_cache, "tcp:127.0.0.1:80") == nullptr);
    REQUIRE(model_map_get(&tctx.intercepts_cache, "tcp:192.168.0.88:80") == intercept_s1);
    REQUIRE(model_map_get(&tctx.intercepts_cache, "tcp:192.168.0.10:80") == intercept_s2);
    REQUIRE(model_map_get(&tctx.intercepts_cache, "tcp:192.168.0.10:81") == intercept_s3);

    // todo hostname and wildcard dns matching
}

TEST_CASE("address_conversion", "[address]") {
    const char *ip6_str = "2768:8631:c02:ffc9::1308";
    ip_addr_t ip6;
    ipaddr_aton(ip6_str, &ip6);
    ziti_address za_from_ip6;
    ziti_address_from_ip_addr(&za_from_ip6, &ip6);

    ziti_address za_from_str;
    ziti_address_from_string(&za_from_str, ip6_str);

    char za_str[128];
    ziti_address_print(za_str, sizeof(za_str), &za_from_ip6);
    fprintf(stderr, "%s converted to %s\n", ip6_str, za_str);
    REQUIRE(ziti_address_match(&za_from_ip6, &za_from_str) == 0);
}