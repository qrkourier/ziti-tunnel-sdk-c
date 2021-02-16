#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/kern_control.h>
#include <net/if.h>
#include <net/if_utun.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>
#include <sys/kern_event.h>
#include <sys/uio.h>

#include "utun.h"

int utun_close(struct netif_handle_s *tun) {
    int r = 0;

    if (tun == NULL) {
        return 0;
    }

    if (tun->fd > 0) {
        r = close(tun->fd);
    }

    free(tun);
    return r;
}

static inline ssize_t utun_data_len(ssize_t len) {
    if (len > 0) {
        return (len > sizeof(u_int32_t)) ? len - sizeof(u_int32_t) : 0;
    } else {
        return len;
    }
}

ssize_t utun_read(netif_handle tun, void *buf, size_t len) {
    u_int32_t type;
    struct iovec iv[2];

    iv[0].iov_base = &type;
    iv[0].iov_len = sizeof(type);
    iv[1].iov_base = buf;
    iv[1].iov_len = len;

    return utun_data_len(readv(tun->fd, iv, 2));
}

ssize_t utun_write(netif_handle tun, const void *buf, size_t len) {
    u_int32_t type;
    struct iovec iv[2];
    struct ip *iph = (struct ip *)buf;

    if (iph->ip_v == 6) {
        type = htonl(AF_INET6);
    } else {
        type = htonl(AF_INET);
    }

    iv[0].iov_base = &type;
    iv[0].iov_len = sizeof(type);
    iv[1].iov_base = (void *)buf;
    iv[1].iov_len = len;

    return utun_data_len(writev(tun->fd, iv, 2));
}

int utun_uv_poll_init(netif_handle tun, uv_loop_t *loop, uv_poll_t *tun_poll_req) {
    return uv_poll_init(loop, tun_poll_req, tun->fd);
}

/**
 * this could also be done with `route` command if interface has local address assigned:
 * - ifconfig utun2 169.254.1.2/32 169.254.1.2
 * - route add -host 2.2.2.2 -interface utun2
 * - route add -host 1.2.3.4 -interface utun2
 *
 * - ifconfig utun4 inet6 2001:DB8:2:2::2/128
 * - ifconfig utun4 inet6 2001:DB8:2:2::3/128 2001:DB8:2:2::3
 */
void utun_add_route(netif_handle tun, const char *ip) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "route add -host %s -interface %s", ip, tun->name);
    system(cmd);
}

/**
 * open a utun device
 * @param num populated with the unit number of the utun device that was opened
 * @return file descriptor to opened utun
 */
netif_driver utun_open(uint32_t tun_ip, uint32_t dns_ip, const char *dns_block, char *error, size_t error_len) {
    if (error != NULL) {
        memset(error, 0, error_len * sizeof(char));
    }

    struct netif_handle_s *tun = calloc(1, sizeof(struct netif_handle_s));
    if (tun == NULL) {
        if (error != NULL) {
            snprintf(error, error_len, "failed to allocate utun");
        }
        return NULL;
    }

    struct sockaddr_ctl addr;

    if ((tun->fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL)) < 0) {
        if (error != NULL) {
            snprintf(error, error_len, "failed to create control socket: %s", strerror(errno));
        }
        utun_close(tun);
        return NULL;
    }

    struct ctl_info info;
    memset(&info, 0, sizeof (info));
    strncpy(info.ctl_name, UTUN_CONTROL_NAME, strlen(UTUN_CONTROL_NAME));
    if (ioctl(tun->fd, CTLIOCGINFO, &info) == -1) {
        if (error != NULL) {
            snprintf(error, error_len, "ioctl(CTLIOCGINFO) failed: %s", strerror(errno));
        }
        utun_close(tun);
        return NULL;
    }

    addr.sc_id = info.ctl_id;
    addr.sc_len = sizeof(addr);
    addr.sc_family = AF_SYSTEM;
    addr.ss_sysaddr = AF_SYS_CONTROL;
    addr.sc_unit = 0; // use first available unit

    if (connect(tun->fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if (error != NULL) {
            snprintf(error, error_len, "failed to open utun device: %s", strerror(errno));
        }
        utun_close(tun);
        return NULL;
    }

    struct ifreq if_req;
    socklen_t ifname_req_size = sizeof(if_req);
    if (getsockopt(tun->fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, &if_req, &ifname_req_size) == -1) {
        if (error != NULL) {
            snprintf(error, error_len, "failed to get ifname: %s", strerror(errno));
        }
        utun_close(tun);
        return NULL;
    }
    strncpy(tun->name, if_req.ifr_name, sizeof(tun->name));

    int s = socket(PF_LOCAL, SOCK_DGRAM, 0);
    if (s < 0) {
        if (error != NULL) {
            snprintf(error, error_len, "failed to get socket: %s", strerror(errno));
        }
        return NULL;
    }

    if_req.ifr_mtu = 0xFFFF;
    if (ioctl(s, SIOCSIFMTU, &if_req) == -1) {
        if (error != NULL) {
            snprintf(error, error_len, "failed to get mtu: %s", strerror(errno));
        }
        utun_close(tun);
        return NULL;
    }

    struct netif_driver_s *driver = calloc(1, sizeof(struct netif_driver_s));
    if (driver == NULL) {
        if (error != NULL) {
            snprintf(error, error_len, "failed to allocate netif_driver_s");
            utun_close(tun);
        }
        return NULL;
    }

    driver->handle       = tun;
    driver->read         = utun_read;
    driver->write        = utun_write;
    driver->uv_poll_init = utun_uv_poll_init;
    driver->add_route    = utun_add_route;
    driver->close        = utun_close;

    if (dns_block) {
        char cmd[1024];
        int ip_len = (int)strlen(dns_block);
        const char *prefix_sep = strchr(dns_block, '/');
        if (prefix_sep != NULL) {
            ip_len = (int)(prefix_sep - dns_block);
        }
        // add address to interface. darwin utun devices may only have "point to point" addresses
        snprintf(cmd, sizeof(cmd), "ifconfig %s %.*s %.*s", tun->name, ip_len, dns_block, ip_len, dns_block);
        system(cmd);

        // add a route for the subnet if one was specified
        if (prefix_sep != NULL) {
            snprintf(cmd, sizeof(cmd), "route add -net %s -interface %s", dns_block, tun->name);
            system(cmd);
        }
    }

    if (dns_ip && FALSE /* puts our resolver first, but requires proxying for hostnames that we don't know */) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "scutil <<EOF\n"
                                   "d.init\n"
                                   "d.add ServerAddresses * %s\n"
                                   "d.add SupplementalMatchDomains * \"\"\n"
                                   "set State:/Network/Service/ZitiEdgeTunnel.%s/DNS\n"
                                   "quit\nEOF",
                 inet_ntoa(*(struct in_addr*)&dns_ip), tun->name);
        int rc = system(cmd);
        if (rc != 0) {
            snprintf(error, error_len, "dns resolver setup failed");
        }
    }

    return driver;
}