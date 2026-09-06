/*
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfHSM.
 *
 * wolfHSM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfHSM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with wolfHSM.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
 * examples/posix/wh_ipse_demo/wh_ipse_client.c
 *
 * The host process: a wolfHSM client connected to the "IPSE firmware"
 * process over the MEI simulator's named socket, running a TLS 1.3
 * handshake whose client-side crypto (ECDHE, ECDSA verify, AES-GCM
 * records, SHA-2) is offloaded to the engine through the wolfHSM crypto
 * callback (WH_DEV_ID).  The TLS peer is a local software-crypto server
 * thread, so the demo is self-contained.
 *
 * Usage: wh_ipse_client [socket_path] (start wh_ipse_server first)
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>

#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/ssl.h"
#include "wolfssl/certs_test.h"

#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_client.h"

#include "port/intel/mei/intel_mei_transport.h"
#include "port/intel/mei/intel_mei_sim.h"

#include "wh_ipse_demo.h"

#define DEMO_MSG "hello from the IPSE-offloaded TLS 1.3 client"

/* Software-crypto TLS 1.3 server thread (the demo's remote peer) */
static void* _tlsServerTask(void* unused)
{
    int                ret = -1;
    int                listenFd;
    int                connFd = -1;
    int                enable = 1;
    struct sockaddr_in addr;
    WOLFSSL_CTX*       ctx = NULL;
    WOLFSSL*           ssl = NULL;
    char               buf[256];

    (void)unused;

    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        return NULL;
    }
    (void)setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &enable,
                     sizeof(enable));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(WH_IPSE_DEMO_TLS_PORT);
    if ((bind(listenFd, (struct sockaddr*)&addr, sizeof(addr)) != 0) ||
        (listen(listenFd, 1) != 0)) {
        close(listenFd);
        return NULL;
    }

    ctx = wolfSSL_CTX_new(wolfTLSv1_3_server_method());
    if (ctx != NULL) {
        ret = wolfSSL_CTX_use_certificate_buffer(ctx, serv_ecc_der_256,
            sizeof_serv_ecc_der_256, WOLFSSL_FILETYPE_ASN1);
        if (ret == WOLFSSL_SUCCESS) {
            ret = wolfSSL_CTX_use_PrivateKey_buffer(ctx, ecc_key_der_256,
                sizeof_ecc_key_der_256, WOLFSSL_FILETYPE_ASN1);
        }
    }
    if ((ctx != NULL) && (ret == WOLFSSL_SUCCESS)) {
        struct pollfd pfd;
        pfd.fd      = listenFd;
        pfd.events  = POLLIN;
        pfd.revents = 0;
        /* Bounded wait so a client that fails before connecting does not leave
         * this thread blocked in accept() forever, hanging pthread_join(). */
        if (poll(&pfd, 1, 5000) == 1) {
            connFd = accept(listenFd, NULL, NULL);
        }
        if (connFd >= 0) {
            ssl = wolfSSL_new(ctx);
            if (ssl != NULL) {
                wolfSSL_set_fd(ssl, connFd);
                ret = wolfSSL_accept(ssl);
                if (ret == WOLFSSL_SUCCESS) {
                    memset(buf, 0, sizeof(buf));
                    ret = wolfSSL_read(ssl, buf, (int)sizeof(buf) - 1);
                    if (ret > 0) {
                        printf("[tls server] got: %s\n", buf);
                        (void)wolfSSL_write(ssl, buf, ret);
                    }
                }
                else {
                    printf("[tls server] accept failed: %d\n",
                           wolfSSL_get_error(ssl, ret));
                }
                (void)wolfSSL_shutdown(ssl);
                wolfSSL_free(ssl);
            }
            close(connFd);
        }
    }
    if (ctx != NULL) {
        wolfSSL_CTX_free(ctx);
    }
    close(listenFd);
    return NULL;
}

int main(int argc, char** argv)
{
    int                rc;
    int                ret     = -1;
    int                sockFd  = -1;
    int                success = 0;
    uint32_t           clientId = 0;
    uint32_t           serverId = 0;
    pthread_t          serverThread;
    void*              retval = NULL;
    struct sockaddr_in addr;
    char               buf[256];

    /* wolfHSM client over the MEI sim named socket */
    static intelMeiSimContext       simCtx;
    static intelMeiSimConfig        simConfig;
    static intelMeiTransportContext tpCtx;
    static intelMeiTransportConfig  tpConfig;
    static whTransportClientCb      tpCb[1] = {IMT_CLIENT_CB};
    static whCommClientConfig       commConfig[1];
    static whClientConfig           clientConfig[1];
    static whClientContext          client[1];

    WOLFSSL_CTX* ctx = NULL;
    WOLFSSL*     ssl = NULL;

    memset(&simCtx, 0, sizeof(simCtx));
    memset(&simConfig, 0, sizeof(simConfig));
    simConfig.path     = (argc > 1) ? argv[1] : WH_IPSE_DEMO_SOCKET_PATH;
    simConfig.maxMsg   = WH_IPSE_DEMO_MAX_MSG;
    simConfig.isServer = 0;

    memset(&tpConfig, 0, sizeof(tpConfig));
    tpConfig.ops        = &intelMeiSimDevOps;
    tpConfig.devContext = &simCtx;
    tpConfig.devConfig  = &simConfig;

    memset(commConfig, 0, sizeof(commConfig));
    commConfig[0].transport_cb      = tpCb;
    commConfig[0].transport_context = &tpCtx;
    commConfig[0].transport_config  = &tpConfig;
    commConfig[0].client_id         = 12;

    memset(clientConfig, 0, sizeof(clientConfig));
    clientConfig[0].comm = commConfig;

    printf("IPSE demo client: connecting to engine at %s\n", simConfig.path);
    rc = wh_Client_Init(client, clientConfig);
    if (rc != 0) {
        printf("wh_Client_Init failed: %d (is wh_ipse_server running?)\n",
               rc);
        return 1;
    }
    rc = wh_Client_CommInit(client, &clientId, &serverId);
    if (rc != 0) {
        printf("wh_Client_CommInit failed: %d\n", rc);
        (void)wh_Client_Cleanup(client);
        return 1;
    }
    printf("Connected to IPSE wolfHSM server (client %u, server %u); "
           "crypto devId 0x%08x\n",
           (unsigned int)clientId, (unsigned int)serverId,
           (unsigned int)WH_DEV_ID);

    /* Local software-crypto TLS 1.3 peer */
    wolfSSL_Init();
    rc = pthread_create(&serverThread, NULL, _tlsServerTask, NULL);
    if (rc != 0) {
        printf("server thread create failed\n");
        goto exit;
    }
    {
        /* let the peer reach accept() */
        struct timespec ts;
        ts.tv_sec  = 0;
        ts.tv_nsec = 200000000L;
        (void)nanosleep(&ts, NULL);
    }

    /* TLS 1.3 client with all crypto routed to the engine */
    ctx = wolfSSL_CTX_new(wolfTLSv1_3_client_method());
    if (ctx == NULL) {
        goto join;
    }
    wolfSSL_CTX_SetDevId(ctx, WH_DEV_ID);
    ret = wolfSSL_CTX_load_verify_buffer(ctx, ca_ecc_cert_der_256,
        sizeof_ca_ecc_cert_der_256, WOLFSSL_FILETYPE_ASN1);
    if (ret != WOLFSSL_SUCCESS) {
        printf("load CA failed: %d\n", ret);
        goto join;
    }

    sockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFd < 0) {
        goto join;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(WH_IPSE_DEMO_TLS_PORT);
    if (connect(sockFd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        printf("TCP connect failed\n");
        goto join;
    }

    ssl = wolfSSL_new(ctx);
    if (ssl == NULL) {
        goto join;
    }
    wolfSSL_set_fd(ssl, sockFd);
    ret = wolfSSL_connect(ssl);
    if (ret == WOLFSSL_SUCCESS) {
        printf("TLS 1.3 handshake complete: %s / %s\n",
               wolfSSL_get_version(ssl), wolfSSL_get_cipher(ssl));
        ret = wolfSSL_write(ssl, DEMO_MSG, (int)strlen(DEMO_MSG));
        if (ret > 0) {
            memset(buf, 0, sizeof(buf));
            ret = wolfSSL_read(ssl, buf, (int)sizeof(buf) - 1);
            if ((ret > 0) && (strcmp(buf, DEMO_MSG) == 0)) {
                printf("Echo verified over the offloaded connection\n");
                success = 1;
            }
        }
    }
    else {
        printf("wolfSSL_connect failed: %d\n", wolfSSL_get_error(ssl, ret));
    }
    (void)wolfSSL_shutdown(ssl);

join:
    if (ssl != NULL) {
        wolfSSL_free(ssl);
    }
    if (sockFd >= 0) {
        close(sockFd);
    }
    if (ctx != NULL) {
        wolfSSL_CTX_free(ctx);
    }
    (void)pthread_join(serverThread, &retval);
exit:
    (void)wh_Client_CommClose(client);
    (void)wh_Client_Cleanup(client);
    intelMeiSim_Destroy(&simCtx);
    wolfSSL_Cleanup();

    printf("IPSE demo client: %s\n", success ? "SUCCESS" : "FAILED");
    return success ? 0 : 1;
}
