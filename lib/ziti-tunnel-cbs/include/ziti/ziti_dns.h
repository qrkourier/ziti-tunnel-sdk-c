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

#ifndef ZITI_TUNNEL_SDK_C_ZITI_DNS_H
#define ZITI_TUNNEL_SDK_C_ZITI_DNS_H

#include <ziti/ziti_tunnel.h>

#include "ziti_tunnel_cbs.h"

#define DNS_NO_ERROR 0
#define DNS_FORMERR  1
#define DNS_SERVFAIL 2
#define DNS_NXDOMAIN 3
#define DNS_NOT_IMPL 4
#define DNS_REFUSE   5
#define DNS_NOTZONE  9

#ifdef __cplusplus
extern "C" {
#endif

int ziti_dns_setup(tunneler_context tnlr, const char *dns_addr, const char *dns_cidr);

int ziti_dns_set_upstream(uv_loop_t *l, tunnel_upstream_dns_array upstreams);

const ip_addr_t *ziti_dns_register_hostname(const ziti_address *addr, void *intercept);

const char *ziti_dns_reverse_lookup_domain(const ip_addr_t *addr);

const char *ziti_dns_reverse_lookup(const char *ip_addr);

void ziti_dns_deregister_intercept(void *intercept);

#ifdef __cplusplus
};
#endif

#endif //ZITI_TUNNEL_SDK_C_ZITI_DNS_H
