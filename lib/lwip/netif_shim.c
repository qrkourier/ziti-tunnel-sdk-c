//#include <sys/uio.h>

#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS 1

#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "nf/netif_driver.h"
#include "netif_shim.h"

#define IFNAME0 't'
#define IFNAME1 'n'

/* max ipv4 MTU */
#define BUFFER_SIZE 64 * 1024

static char shim_buffer[BUFFER_SIZE];
/**
 * This function is called by the TCP/IP stack when an IP packet should be sent.
 */
static err_t netif_shim_output(struct netif *netif, struct pbuf *p, const ip4_addr_t *ipaddr) {
    netif_driver dev = netif->state;

    u16_t copied = pbuf_copy_partial(p, shim_buffer, p->tot_len, 0);
    if (copied != p->tot_len) {
        fprintf(stderr, "pbuf_copy_partial() failed %d/%d", copied, p->tot_len);
        return ERR_BUF; // ?
    }
    dev->write(dev->handle, shim_buffer, p->tot_len);
    return ERR_OK;
}

/**
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface.
 */
void netif_shim_input(struct netif *netif) {
    netif_driver dev = netif->state;
    char buf[BUFFER_SIZE];

    ssize_t nr = dev->read(dev->handle, buf, sizeof(buf));

    if ((nr <= 0) || (nr > 0xffff)) {
        return;
    }

    on_packet(buf, nr, netif);
}

void on_packet(const char *buf, ssize_t nr, void *ctx) {
    struct netif *netif = ctx;
    struct pbuf *p;
    /* We allocate a pbuf chain of pbufs from the pool. */
    p = pbuf_alloc(PBUF_LINK, (u16_t) nr, PBUF_POOL);

    if (p != NULL) {
        pbuf_take(p, buf, (u16_t) nr);
        /* acknowledge that packet has been read(); */
    } else {
        /* drop packet(); */
        printf("pbuf_alloc failed\n");
        return;
    }

    err_t err = netif->input(p, netif);
    if (err != ERR_OK) {
        printf("============================> tunif_input: netif input error %s\n", lwip_strerr(err));
        pbuf_free(p);
    }
}

/**
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 */
err_t netif_shim_init(struct netif *netif) {
    netif->name[0] = IFNAME0;
    netif->name[1] = IFNAME1;
    netif->output = netif_shim_output;

    return ERR_OK;
}