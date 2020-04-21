#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "uv.h"
#include "nf/ziti.h"
#include "nf/ziti_tunneler.h"
#include "nf/ziti_tunneler_cbs.h"

#if __APPLE__ && __MACH__
#include "netif_driver/darwin/utun.h"
#elif __linux__
#include "netif_driver/linux/tun.h"
#else
#error "please port this file to your operating system"
#endif

/** callback from ziti SDK when a new service becomes available to our identity */
void on_service(nf_context nf_ctx, ziti_service *service, int status, void *tnlr_ctx) {
    printf("service_available: %s\n", service->name);

    ziti_context *ziti_ctx = malloc(sizeof(ziti_context));
    if (ziti_ctx == NULL) {
        fprintf(stderr, "failed to allocate dial context\n");
        return;
    }
    ziti_ctx->nf_ctx = nf_ctx;

    ziti_intercept intercept;
    if (status == ZITI_OK && (service->perm_flags & ZITI_CAN_DIAL)) {
        int rc = ziti_service_get_config(service, "ziti-tunneler-client.v1", &intercept, parse_ziti_intercept);
        if (rc == 0) {
            NF_tunneler_intercept_v1(tnlr_ctx, ziti_ctx, service->name, intercept.hostname, intercept.port);
            free(intercept.hostname);
        }
        printf("ziti_service_get_config rc: %d\n", rc);
    }
}

const char *cfg_types[] = { "ziti-tunneler-client.v1", "ziti-tunneler-server.v1", NULL };

static void on_nf_init(nf_context nf_ctx, int status, void *init_ctx) {
    if (status != ZITI_OK) {
        fprintf(stderr, "failed to initialize ziti\n");
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    uv_loop_t *nf_loop = uv_default_loop();
    if (nf_loop == NULL) {
        fprintf(stderr, "failed to initialize default uv loop\n");
        return 1;
    }

    netif_driver tun;
    char tun_error[64];
#if __APPLE__ && __MACH__
    tun = utun_open(tun_error, sizeof(tun_error));
#elif __linux__
    tun = tun_open(tun_error, sizeof(tun_error));
#endif

    if (tun == NULL) {
        fprintf(stderr, "failed to open network interface: %s\n", tun_error);
        return 1;
    }

    tunneler_sdk_options tunneler_opts = {
            .netif_driver = tun,
            .ziti_dial = ziti_sdk_c_dial,
            .ziti_close = ziti_sdk_c_close,
            .ziti_write = ziti_sdk_c_write
    };
    tunneler_context tnlr_ctx = NF_tunneler_init(&tunneler_opts, nf_loop);

    nf_options opts = {
            .init_cb = on_nf_init,
            .config = "/Users/scarey/Downloads/localdev-0.13.json",
            .service_cb = on_service,
            .ctx = tnlr_ctx, /* this is passed to the service_cb */
            .refresh_interval = 10,
            .config_types = cfg_types,
    };

    if (NF_init_opts(&opts, nf_loop, NULL) != 0) {
        fprintf(stderr, "failed to initialize ziti\n");
        return 1;
    }

    if (uv_run(nf_loop, UV_RUN_DEFAULT) != 0) {
        fprintf(stderr, "failed to run event loop\n");
        exit(1);
    }

    free(tnlr_ctx);
    return 0;
}