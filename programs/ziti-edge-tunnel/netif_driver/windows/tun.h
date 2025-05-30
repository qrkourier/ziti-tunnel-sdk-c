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

//
// Created by eugene on 4/21/2021.
//

#ifndef ZITI_TUNNEL_SDK_C_TUN_H
#define ZITI_TUNNEL_SDK_C_TUN_H

extern netif_driver tun_open(struct uv_loop_s *loop, uint32_t tun_ip, const char *cidr, char *error, size_t error_len);

extern int set_dns(netif_driver tun, uint32_t dns_ip);

#endif //ZITI_TUNNEL_SDK_C_TUN_H
