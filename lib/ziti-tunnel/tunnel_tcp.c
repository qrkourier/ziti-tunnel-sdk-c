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

#include <stdlib.h>
#include <string.h>
#include "tunnel_tcp.h"
#include "lwip_cloned_fns.h"
#include "ziti_tunnel_priv.h"
#include "ziti/sys/queue.h"

#if _WIN32
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#endif

#define LOG_STATE(level, op, pcb, ...) do { \
    io_ctx_t *io = ((io_ctx_t *)(pcb)->callback_arg); \
    tunneler_io_context tnlr_io = io ? io->tnlr_io : NULL; \
    const char *service_name = tnlr_io ? tnlr_io->service_name : ""; \
    TNL_LOG(level, op " src[%s] dst[%s] state[%d/%s] flags[%#0x] service[%s]", ##__VA_ARGS__, \
            tnlr_io ? tnlr_io->client : "", \
            tnlr_io ? tnlr_io->intercepted : "", \
            pcb->state, tcp_state_str(pcb->state), pcb->flags, service_name); \
} while (0)

#define tcp_states(XX)\
  XX(CLOSED)\
  XX(LISTEN)\
  XX(SYN_SENT)\
  XX(SYN_RCVD)\
  XX(ESTABLISHED)\
  XX(FIN_WAIT_1)\
  XX(FIN_WAIT_2)\
  XX(CLOSE_WAIT)\
  XX(CLOSING)\
  XX(LAST_ACK)\
  XX(TIME_WAIT)

#define tcp_str(s) #s,
static const char* tcp_labels[] = {
        tcp_states(tcp_str)
};

static const char* tcp_state_str(int st) {
    if (st < 0 || st >= sizeof(tcp_labels)/sizeof(tcp_labels[0])) {
        return "unknown";
    }
    return tcp_labels[st];
}

/** called by lwip when a client sends a SYN segment to an intercepted address.
 * this only exists to appease lwip */
static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    TNL_LOG(DEBUG, "on_accept: %d", err);
    return ERR_OK;
}

/** create a tcp connection to be managed by lwip */
static struct tcp_pcb *new_tcp_pcb(ip_addr_t src, ip_addr_t dest, struct tcp_hdr *tcphdr, struct pbuf *p) {
    /** associate all injected PCBs with the same phony listener to appease some LWIP checks */
    static struct tcp_pcb_listen * phony_listener = NULL;
    if (phony_listener == NULL) {
        if ((phony_listener = memp_malloc(MEMP_TCP_PCB_LISTEN)) == NULL) {
            TNL_LOG(ERR, "failed to allocate listener");
            return NULL;
        }
        phony_listener->accept = on_accept;
    }
    struct tcp_pcb *npcb = tcp_new();
    if (npcb == NULL) {
        TNL_LOG(ERR, "tcp_new failed");
        return NULL;
    }
    /* Set up the new PCB. */
    ip_addr_copy(npcb->local_ip, dest);
    ip_addr_copy(npcb->remote_ip, src);
    npcb->local_port = lwip_ntohs(tcphdr->dest);
    npcb->remote_port = lwip_ntohs(tcphdr->src);
    npcb->state = SYN_RCVD;
    npcb->rcv_nxt = lwip_ntohl(tcphdr->seqno) + 1;
    npcb->rcv_ann_right_edge = npcb->rcv_nxt;
    u32_t iss = tcp_next_iss(npcb);
    npcb->snd_wl2 = iss;
    npcb->snd_nxt = iss;
    npcb->lastack = iss;
    npcb->snd_lbb = iss;
    npcb->snd_wl1 = lwip_ntohl(tcphdr->seqno) - 1;/* initialise to seqno-1 to force window update */
    /* allocate a listener and set accept fn to appease lwip */
    npcb->listener = phony_listener;
    npcb->netif_idx = netif_get_index(netif_default);

    /* Register the new PCB so that we can begin receiving segments for it. */
    TCP_REG_ACTIVE(npcb);

    /* Parse any options in the SYN. */
    tunneler_tcp_input(p);
    tunneler_tcp_parseopt(npcb);
    npcb->snd_wnd = lwip_ntohs(tcphdr->wnd);
    npcb->snd_wnd_max = npcb->snd_wnd;

#if TCP_CALCULATE_EFF_SEND_MSS
    npcb->mss = tcp_eff_send_mss(npcb->mss, &npcb->local_ip, &npcb->remote_ip);
#endif /* TCP_CALCULATE_EFF_SEND_MSS */
    TNL_LOG(DEBUG, "snd_wnd: %d, snd_snd_max: %d, mss: %d", npcb->snd_wnd, npcb->snd_wnd_max, npcb->mss);

    MIB2_STATS_INC(mib2.tcppassiveopens);

#if LWIP_TCP_PCB_NUM_EXT_ARGS
    if (tcp_ext_arg_invoke_callbacks_passive_open(pcb, npcb) != ERR_OK) {
      tcp_abandon(npcb, 0);
      return NULL;
    }
#endif
    return npcb;
}

/**
 * called by lwip when a client writes to an intercepted connection.
 * pbuf will be null if client has closed the connection.
 */
static err_t on_tcp_client_data(void *io_ctx, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (io_ctx == NULL) {
        TNL_LOG(INFO, "conn was closed err=%d", err);
        if (p != NULL) {
            pbuf_free(p);
        }
        return ERR_CONN;
    }
    LOG_STATE(VERBOSE, "status %d", pcb, err);
    struct io_ctx_s *io = (struct io_ctx_s *)io_ctx;

    if (err == ERR_OK && p == NULL) {
        TNL_LOG(DEBUG, "client sent FIN: client=%s, service=%s", io->tnlr_io->client, io->tnlr_io->service_name);
        LOG_STATE(DEBUG, "FIN received", pcb);
        io->close_write_fn(io->ziti_io);
        return err;
    }

    p = pbuf_coalesce(p, PBUF_RAW);
    u16_t len = p->len;
    struct write_ctx_s *wr_ctx = calloc(1, sizeof(struct write_ctx_s));
    wr_ctx->pbuf = p;
    wr_ctx->tcp = pcb;
    wr_ctx->ack = tunneler_tcp_ack;
    ssize_t s = io->write_fn(io->ziti_io, wr_ctx, p->payload, len);
    if (s == ERR_WOULDBLOCK) {
        // apply backpressure -- let LWIP keep the data and retry later
        TNL_LOG(VERBOSE, "ziti_write indicated backpressure: service=%s, client=%s", io->tnlr_io->service_name, io->tnlr_io->client);
        free(wr_ctx);
        return ERR_WOULDBLOCK;
    } else if (s < 0) {
        TNL_LOG(ERR, "ziti_write failed: service=%s, client=%s, ret=%ld", io->tnlr_io->service_name, io->tnlr_io->client, s);
        // tell lwip to abort this connection immediately, and null the PCB to prevent ziti_close callback from (double) closing
        tcp_abort(io->tnlr_io->tcp);
        io->tnlr_io->tcp = NULL;
        io->close_fn(io->ziti_io);
        free(wr_ctx);
        pbuf_free(p);
        return ERR_ABRT;
    }
    return ERR_OK;
}

/** called by lwip when an error has occurred on a tcp connection.
 * the corresponding pcb is not valid by the time this fn is called. */
static void on_tcp_client_err(void *io_ctx, err_t err) {
    struct io_ctx_s *io = io_ctx;
    // we initiated close and cleared arg err should be ERR_ABRT
    if (io_ctx == NULL) {
        TNL_LOG(TRACE, "client pcb(<unknown>) finished err=%d", err);
    }
    else {
        const char *client = "<unknown>";
        if (io->tnlr_io != NULL) {
            client = io->tnlr_io->client;
            // null our pcb so tunneler_tcp_close doesn't try to close it.
            io->tnlr_io->tcp = NULL;
        }
        TNL_LOG(ERR, "client=%s err=%d, terminating connection", client, err);
        io->close_fn(io->ziti_io);
    }
}

ssize_t tunneler_tcp_write(struct tcp_pcb *pcb, const void *data, size_t len) {
    if (pcb == NULL) {
        TNL_LOG(WARN, "null pcb");
        return -1;
    }

    int qlen = tcp_sndqueuelen(pcb);
    if (qlen > TCP_SND_QUEUELEN) {
        TNL_LOG(WARN, "sndqueuelen limit reached (%d > %d)", qlen, TCP_SND_QUEUELEN);
    }
    // avoid ERR_MEM.
    size_t sendlen = MIN(len, tcp_sndbuf(pcb));
    LOG_STATE(TRACE, "sendlen=%zd", pcb, sendlen);
    if (sendlen > 0) {
        err_t w_err = tcp_write(pcb, data, (u16_t) sendlen,
                                TCP_WRITE_FLAG_COPY); // TODO hold data until client acks... via on_client_ack maybe? then we wouldn't need to copy here.
        if (w_err != ERR_OK) {
            TNL_LOG(ERR, "failed to tcp_write %d (%ld, %zd)", w_err, sendlen, len);
            return -1;
        }

        if (tcp_output(pcb) != ERR_OK) {
            TNL_LOG(ERR, "failed to tcp_output");
            return -1;
        }
    }
    return sendlen;
}

void tunneler_tcp_ack(struct write_ctx_s *write_ctx) {
    struct write_ctx_s *wr_ctx = write_ctx;
    tcp_recved(wr_ctx->tcp, wr_ctx->pbuf->len);
    pbuf_free(wr_ctx->pbuf);
}

int tunneler_tcp_close_write(struct tcp_pcb *pcb) {
    if (pcb == NULL) {
        TNL_LOG(WARN, "null pcb");
        return 0;
    }
    LOG_STATE(DEBUG, "closing write", pcb);
    err_t err = tcp_shutdown(pcb, 0, 1);
    if (err != ERR_OK) {
        LOG_STATE(ERR, "tcp_shutdown failed: err=%d", pcb, err);
        return -1;
    }
    LOG_STATE(DEBUG, "closed write", pcb);

    return 0;
}

int tunneler_tcp_close(struct tcp_pcb *pcb) {
    if (pcb == NULL) {
        TNL_LOG(DEBUG, "null pcb");
        return 0;
    }
    LOG_STATE(DEBUG, "closing", pcb);
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    if (pcb->state == CLOSED) {
        return 0;
    }
    if (pcb->state < ESTABLISHED) {
        TNL_LOG(DEBUG, "closing connection before handshake complete. sending RST to client");
        tcp_abandon(pcb, 1);
        return -1;
    }
    err_t err = tcp_close(pcb);
    if (err != ERR_OK) {
        LOG_STATE(ERR, "tcp_close failed; err=%d", pcb, err);
        return -1;
    }
    LOG_STATE(DEBUG, "closed", pcb);
    return 0;
}

void tunneler_tcp_dial_completed(struct io_ctx_s *io, bool ok) {
    if (io == NULL) {
        TNL_LOG(WARN, "null io_ctx");
        return;
    }

    struct tcp_pcb *pcb = io->tnlr_io->tcp;
    if (pcb == NULL) {
        TNL_LOG(ERR, "tcp connection with %s is no longer viable", io->tnlr_io->client);
        // no need to close the ziti side here, since it was done in the tcp error callback.
        return;
    }

    if (!ok) {
        TNL_LOG(VERBOSE, "ziti dial failed. not sending SYN to client.");
        return;
    }
    ip_set_option(pcb, SOF_KEEPALIVE);
    tcp_recv(pcb, on_tcp_client_data);

    /* Send a SYN|ACK together with the MSS option. */
    err_t rc = tcp_enqueue_flags(pcb, TCP_SYN | TCP_ACK);
    if (rc != ERR_OK) {
        tcp_abandon(pcb, 1);
        return;
    }

    tcp_output(io->tnlr_io->tcp);
}

static tunneler_io_context new_tunneler_io_context(tunneler_context tnlr_ctx, const char *service_name, const char *src, const char *dst, struct tcp_pcb *pcb) {
    struct tunneler_io_ctx_s *ctx = calloc(1, sizeof(struct tunneler_io_ctx_s));
    if (ctx == NULL) {
        TNL_LOG(ERR, "failed to allocate tunneler_io_ctx");
        return NULL;
    }
    ctx->tnlr_ctx = tnlr_ctx;
    ctx->service_name = strdup(service_name);
    snprintf(ctx->client, sizeof(ctx->client), "tcp:%s:%d", src, pcb->remote_port);
    snprintf(ctx->intercepted, sizeof(ctx->intercepted), "tcp:%s:%d", dst, pcb->local_port);
    ctx->proto = tun_tcp;
    ctx->tcp = pcb;
    return ctx;
}

/** called by lwip when a tcp segment arrives. return 1 to indicate that the IP packet was consumed. */
u8_t recv_tcp(void *tnlr_ctx_arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
    tunneler_context tnlr_ctx = tnlr_ctx_arg;

    u16_t iphdr_hlen;
    ip_addr_t src, dst;
    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));
    char ip_version = IPH_V((struct ip_hdr *)(p->payload));

    /* figure out where the tcp header is in the pbuf. don't modify
     * the pbuf until we know that this segment should be intercepted.
     */
    switch (ip_version) {
        case 4: {
            struct ip_hdr *iphdr = p->payload;
            iphdr_hlen = IPH_HL_BYTES(iphdr);
            ip_addr_copy_from_ip4(src, iphdr->src);
            ip_addr_copy_from_ip4(dst, iphdr->dest);
        }
            break;
        case 6: {
            struct ip6_hdr *iphdr = p->payload;
            iphdr_hlen = IP6_HLEN;
            ip_addr_copy_from_ip6_packed(src, iphdr->src);
            ip_addr_copy_from_ip6_packed(dst, iphdr->dest);
        }
            break;
        default:
            TNL_LOG(INFO, "unsupported IP protocol version: %d", ip_version);
            return 0;
    }

    /* reach into the pbuf to get to the TCP header */
    struct tcp_hdr *tcphdr = (struct tcp_hdr *)((char*)p->payload + iphdr_hlen);
    u16_t src_p = lwip_ntohs(tcphdr->src);
    u16_t dst_p = lwip_ntohs(tcphdr->dest);
    char src_str[IPADDR_STRLEN_MAX];
    char dst_str[IPADDR_STRLEN_MAX];
    ipaddr_ntoa_r(&src, src_str, sizeof(src_str));
    ipaddr_ntoa_r(&dst, dst_str, sizeof(dst_str));
    u8_t flags = TCPH_FLAGS(tcphdr);

    if (tunnel_log_level >= TRACE) {
        char flags_str[40] = {0};
        if (flags & TCP_FIN) strcat(flags_str, "FIN,");
        if (flags & TCP_SYN) strcat(flags_str, "SYN,");
        if (flags & TCP_RST) strcat(flags_str, "RST,");
        if (flags & TCP_PSH) strcat(flags_str, "PSH,");
        if (flags & TCP_ACK) strcat(flags_str, "ACK,");
        if (flags & TCP_URG) strcat(flags_str, "URG,");
        if (flags & TCP_ECE) strcat(flags_str, "ECE,");
        if (flags & TCP_CWR) strcat(flags_str, "CWR,");
        if (strlen(flags_str) > 0) flags_str[strlen(flags_str) - 1] = '\0'; // remove trailing comma
        TNL_LOG(TRACE, "received segment src[tcp:%s:%d] dst[tcp:%s:%d] flags[%s]", src_str, src_p, dst_str, dst_p, flags_str);
    }

    if (!(flags & TCP_SYN)) {
        /* this isn't a SYN segment, so let lwip process it */
        return 0;
    }

    intercept_ctx_t *intercept_ctx = lookup_intercept_by_address(tnlr_ctx, "tcp", &src, &dst, dst_p);
    if (intercept_ctx == NULL) {
        /* dst address is not being intercepted. don't consume */
        TNL_LOG(TRACE, "no intercepted addresses match tcp:%s:%d", dst_str, dst_p);
        return 0;
    }

    /* pass the segment to lwip if a matching active connection exists */
    for (struct tcp_pcb *tpcb = tcp_active_pcbs, *prev = NULL; tpcb != NULL; tpcb = tpcb->next) {
        if (tpcb->remote_port == src_p &&
            tpcb->local_port == dst_p &&
            ip_addr_cmp(&tpcb->remote_ip, &src) &&
            ip_addr_cmp(&tpcb->local_ip, &dst)) {
            TNL_LOG(VERBOSE, "received SYN on active connection: client=tcp:%s:%d, service=%s", src_str, src_p, intercept_ctx->service_name);
            /* Move this PCB to the front of the list so that subsequent
               lookups will be faster (we exploit locality in TCP segment
               arrivals). */
            LWIP_ASSERT("tcp_input: pcb->next != pcb (before cache)", tpcb->next != tpcb);
            if (prev != NULL) {
                prev->next = tpcb->next;
                tpcb->next = tcp_active_pcbs;
                tcp_active_pcbs = tpcb;
            } else {
                TCP_STATS_INC(tcp.cachehit);
            }
            LWIP_ASSERT("tcp_input: pcb->next != pcb (after cache)", tpcb->next != tpcb);
            return 0;
        }
        prev = tpcb;
    }

    /* we know this is a SYN segment for an intercepted address, and we will process it */
    ziti_sdk_dial_cb zdial = intercept_ctx->dial_fn ? intercept_ctx->dial_fn : tnlr_ctx->opts.ziti_dial;
    pbuf_remove_header(p, iphdr_hlen);
    struct tcp_pcb *npcb = new_tcp_pcb(src, dst, tcphdr, p);
    if (npcb == NULL) {
        TNL_LOG(ERR, "failed to allocate tcp pcb - TCP connection limit is %d", MEMP_NUM_TCP_PCB);
        goto done;
    }

    struct io_ctx_s *io = calloc(1, sizeof(struct io_ctx_s));
    if (io == NULL) {
        TNL_LOG(ERR, "failed to allocate io_context");
        goto done;
    }
    io->tnlr_io = new_tunneler_io_context(tnlr_ctx, intercept_ctx->service_name, src_str, dst_str, npcb);
    if (io->tnlr_io == NULL) {
        TNL_LOG(ERR, "failed to allocate tunneler io context");
        goto done;
    }
    io->ziti_ctx = intercept_ctx->app_intercept_ctx;
    io->write_fn = intercept_ctx->write_fn ? intercept_ctx->write_fn : tnlr_ctx->opts.ziti_write;
    io->close_write_fn = intercept_ctx->close_write_fn ? intercept_ctx->close_write_fn : tnlr_ctx->opts.ziti_close_write;
    io->close_fn = intercept_ctx->close_fn ? intercept_ctx->close_fn : tnlr_ctx->opts.ziti_close;

    tcp_err(npcb, on_tcp_client_err);
    tcp_arg(npcb, io);

    TNL_LOG(DEBUG, "intercepted address[%s] client[%s] service[%s]", io->tnlr_io->intercepted, io->tnlr_io->client,
            intercept_ctx->service_name);
    void *ziti_io_ctx = zdial(intercept_ctx->app_intercept_ctx, io);
    if (ziti_io_ctx == NULL) {
        TNL_LOG(ERR, "ziti_dial(%s) failed", intercept_ctx->service_name);
        ziti_tunneler_close(io->tnlr_io);
        free(io);
    }
    /* now we wait for the tunneler app to call ziti_tunneler_dial_complete() */

done:
    check_tnlr_timer(tnlr_ctx);
    pbuf_free(p);
    return 1;
}

struct io_ctx_list_s *tunneler_tcp_active(const void *zi_ctx) {
    struct io_ctx_list_s *l = calloc(1, sizeof(struct io_ctx_list_s));
    SLIST_INIT(l);

    for (struct tcp_pcb *tpcb = tcp_active_pcbs; tpcb != NULL; tpcb = tpcb->next) {
        struct io_ctx_s *io = tpcb->callback_arg;
        if (io != NULL) {
            tunneler_io_context tnlr_io = io->tnlr_io;
            if (tnlr_io != NULL) {
                if (io->ziti_ctx == zi_ctx) {
                    struct io_ctx_list_entry_s *n = calloc(1, sizeof(struct io_ctx_list_entry_s));
                    n->io = io;
                    SLIST_INSERT_HEAD(l, n, entries);
                }
            }
        }
    }

    return l;
}

void tunneler_tcp_get_conn(tunnel_ip_conn *conn, struct tcp_pcb *pcb) {
    if (!conn || !pcb) return;
    conn->protocol = strdup("tcp");
    conn->local_ip = strdup(ipaddr_ntoa(&pcb->local_ip));
    conn->local_port = pcb->local_port;
    conn->remote_ip = strdup(ipaddr_ntoa(&pcb->remote_ip));
    conn->remote_port = pcb->remote_port;
    conn->state = strdup(tcp_state_str(pcb->state));
    const char *service = "unknown";
    struct io_ctx_s *io = pcb->callback_arg;
    if (io && io->tnlr_io && io->tnlr_io->service_name) {
        service = io->tnlr_io->service_name;
    }
    conn->service = strdup(service);
}
