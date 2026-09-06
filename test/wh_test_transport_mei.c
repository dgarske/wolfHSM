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
 * test/wh_test_transport_mei.c
 *
 * Exercises the Intel MEI transport fragmentation core over the simulator
 * backend: packet sizes from one byte to WOLFHSM_CFG_COMM_DATA_LEN across
 * several simulated device MTUs, device backpressure (NOTREADY injection)
 * mid-send and mid-receive, and desync/corruption faults.
 */

#include <stdint.h>
#include <string.h>

#include "wolfhsm/wh_settings.h"

#include "wh_test_common.h"

#if defined(WOLFHSM_CFG_TEST_POSIX) && defined(WOLFHSM_CFG_ENABLE_CLIENT) && \
    defined(WOLFHSM_CFG_ENABLE_SERVER)

#include <pthread.h>

#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_server.h"
#include "wolfhsm/wh_nvm.h"
#include "wolfhsm/wh_nvm_flash.h"
#include "wolfhsm/wh_flash_ramsim.h"

#include "port/intel/mei/intel_mei_transport.h"
#include "port/intel/mei/intel_mei_sim.h"

#include "wh_test_transport_mei.h"
#include "wh_test_clientserver.h"
#ifndef WOLFHSM_CFG_NO_CRYPTO
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/random.h"
#include "wh_test_crypto.h"
#ifdef WOLFHSM_CFG_KEYWRAP
#include "wh_test_keywrap.h"
#endif
#endif

#define WH_TEST_MEI_RETRIES 10000

/* Contexts are sized by WH_COMM_MTU; keep them off the stack */
static intelMeiTransportContext gClientCtx;
static intelMeiTransportContext gServerCtx;
static intelMeiSimContext       gClientSim;
static intelMeiSimContext       gServerSim;
static whCommClient             gClient;
static whCommServer             gServer;

static uint8_t gTxBuf[WOLFHSM_CFG_COMM_DATA_LEN];
static uint8_t gRxBuf[WOLFHSM_CFG_COMM_DATA_LEN];

/* Bring up a connected client/server comm pair over the simulator */
static int whTest_MeiSetup(uint32_t maxMsg)
{
    static intelMeiTransportConfig clientTpCfg;
    static intelMeiTransportConfig serverTpCfg;
    static whTransportClientCb     clientTpCb[1] = {IMT_CLIENT_CB};
    static whTransportServerCb     serverTpCb[1] = {IMT_SERVER_CB};
    static whCommClientConfig      clientCfg[1];
    static whCommServerConfig      serverCfg[1];

    WH_TEST_RETURN_ON_FAIL(
        intelMeiSim_CreatePair(&gClientSim, &gServerSim, maxMsg));

    memset(&clientTpCfg, 0, sizeof(clientTpCfg));
    clientTpCfg.ops        = &intelMeiSimDevOps;
    clientTpCfg.devContext = &gClientSim;
    clientTpCfg.devConfig  = NULL;

    memset(&serverTpCfg, 0, sizeof(serverTpCfg));
    serverTpCfg.ops        = &intelMeiSimDevOps;
    serverTpCfg.devContext = &gServerSim;
    serverTpCfg.devConfig  = NULL;

    memset(clientCfg, 0, sizeof(clientCfg));
    clientCfg[0].transport_cb      = clientTpCb;
    clientCfg[0].transport_context = &gClientCtx;
    clientCfg[0].transport_config  = &clientTpCfg;
    clientCfg[0].client_id         = WH_TEST_DEFAULT_CLIENT_ID;

    memset(serverCfg, 0, sizeof(serverCfg));
    serverCfg[0].transport_cb      = serverTpCb;
    serverCfg[0].transport_context = &gServerCtx;
    serverCfg[0].transport_config  = &serverTpCfg;
    serverCfg[0].server_id         = 124;

    memset(&gClient, 0, sizeof(gClient));
    memset(&gServer, 0, sizeof(gServer));
    WH_TEST_RETURN_ON_FAIL(wh_CommClient_Init(&gClient, clientCfg));
    WH_TEST_RETURN_ON_FAIL(wh_CommServer_Init(&gServer, serverCfg, NULL,
                                              NULL));
    return 0;
}

static void whTest_MeiTeardown(void)
{
    (void)wh_CommClient_Cleanup(&gClient);
    (void)wh_CommServer_Cleanup(&gServer);
    intelMeiSim_Destroy(&gClientSim);
    intelMeiSim_Destroy(&gServerSim);
}

/* One echo round trip of dataSz bytes, retrying NOTREADY on every step */
static int whTest_MeiEcho(uint16_t dataSz)
{
    int      sendRc;
    int      recvRc;
    int      retries;
    uint16_t i;
    uint16_t tx_flags = WH_COMM_MAGIC_NATIVE;
    uint16_t tx_type  = 0x42;
    uint16_t tx_seq   = 0;
    uint16_t rx_flags = 0;
    uint16_t rx_type  = 0;
    uint16_t rx_seq   = 0;
    uint16_t rx_len   = 0;

    for (i = 0; i < dataSz; i++) {
        gTxBuf[i] = (uint8_t)(i * 13 + dataSz);
    }

    /* Interleave send and receive so the receiver drains fragments as they
     * arrive; sending a whole packet before draining would need every fragment
     * buffered at once in the AF_UNIX datagram queue (net.unix.max_dgram_qlen).
     * Request: client sends gTxBuf, server reassembles into gRxBuf. */
    sendRc = WH_ERROR_NOTREADY;
    recvRc = WH_ERROR_NOTREADY;
    for (retries = 0; retries < WH_TEST_MEI_RETRIES; retries++) {
        if (sendRc == WH_ERROR_NOTREADY) {
            sendRc = wh_CommClient_SendRequest(&gClient, tx_flags, tx_type,
                                               &tx_seq, dataSz, gTxBuf);
            if (sendRc != 0 && sendRc != WH_ERROR_NOTREADY) {
                break;
            }
        }
        recvRc = wh_CommServer_RecvRequest(&gServer, &rx_flags, &rx_type,
                                           &rx_seq, &rx_len, gRxBuf);
        if (sendRc == 0 && recvRc != WH_ERROR_NOTREADY) {
            break;
        }
    }
    WH_TEST_RETURN_ON_FAIL(sendRc);
    WH_TEST_RETURN_ON_FAIL(recvRc);
    WH_TEST_ASSERT_RETURN(rx_len == dataSz);
    WH_TEST_ASSERT_RETURN(memcmp(gTxBuf, gRxBuf, dataSz) == 0);

    /* Response: echo back from the stable gTxBuf (equal to what was received,
     * asserted above) so the client can drain into gRxBuf without the server's
     * send buffer aliasing the client's receive buffer. */
    memset(gRxBuf, 0, sizeof(gRxBuf));
    sendRc = WH_ERROR_NOTREADY;
    recvRc = WH_ERROR_NOTREADY;
    for (retries = 0; retries < WH_TEST_MEI_RETRIES; retries++) {
        if (sendRc == WH_ERROR_NOTREADY) {
            sendRc = wh_CommServer_SendResponse(&gServer, rx_flags, rx_type,
                                                rx_seq, dataSz, gTxBuf);
            if (sendRc != 0 && sendRc != WH_ERROR_NOTREADY) {
                break;
            }
        }
        recvRc = wh_CommClient_RecvResponse(&gClient, &rx_flags, &rx_type,
                                            &rx_seq, &rx_len, gRxBuf);
        if (sendRc == 0 && recvRc != WH_ERROR_NOTREADY) {
            break;
        }
    }
    WH_TEST_RETURN_ON_FAIL(sendRc);
    WH_TEST_RETURN_ON_FAIL(recvRc);
    WH_TEST_ASSERT_RETURN(rx_len == dataSz);
    WH_TEST_ASSERT_RETURN(memcmp(gTxBuf, gRxBuf, dataSz) == 0);
    return 0;
}

/* Size sweep at one device MTU: single-fragment, boundary, multi-fragment
 * and maximum packets.  The comm layer prepends an 8-byte header, so a data
 * size of (maxMsg - hdr - frag) exactly fills one device message. */
static int whTest_MeiFragSweep(uint32_t maxMsg)
{
    uint16_t oneMsgData;

    WH_TEST_RETURN_ON_FAIL(whTest_MeiSetup(maxMsg));

    /* comm header is the 8 bytes of WH_COMM_MTU above the data length */
    oneMsgData = (uint16_t)(maxMsg - (WH_COMM_MTU - WOLFHSM_CFG_COMM_DATA_LEN)
                            - IMT_FRAG_HDR_SIZE);

    WH_TEST_RETURN_ON_FAIL(whTest_MeiEcho(1));
    WH_TEST_RETURN_ON_FAIL(whTest_MeiEcho(32));
    WH_TEST_RETURN_ON_FAIL(whTest_MeiEcho((uint16_t)(oneMsgData - 1)));
    WH_TEST_RETURN_ON_FAIL(whTest_MeiEcho(oneMsgData));
    WH_TEST_RETURN_ON_FAIL(whTest_MeiEcho((uint16_t)(oneMsgData + 1)));
    WH_TEST_RETURN_ON_FAIL(whTest_MeiEcho(WOLFHSM_CFG_COMM_DATA_LEN));

    /* Device backpressure mid-send and mid-receive */
    gClientSim.notReadyWrites = 3;
    gServerSim.notReadyReads  = 3;
    WH_TEST_RETURN_ON_FAIL(whTest_MeiEcho(WOLFHSM_CFG_COMM_DATA_LEN));

    whTest_MeiTeardown();
    return 0;
}

/* Foreign/corrupt device messages must abort the receiver, not confuse it */
static int whTest_MeiFaults(void)
{
    int     rc;
    uint8_t bogus[8];
    uint16_t rx_flags = 0;
    uint16_t rx_type  = 0;
    uint16_t rx_seq   = 0;
    uint16_t rx_len   = 0;

    WH_TEST_RETURN_ON_FAIL(whTest_MeiSetup(512));

    /* Bad tag: inject via the client-side fd so the server receives it */
    memset(bogus, 0xEE, sizeof(bogus));
    WH_TEST_RETURN_ON_FAIL(
        intelMeiSim_InjectRaw(&gClientSim, bogus, sizeof(bogus)));
    rc = wh_CommServer_RecvRequest(&gServer, &rx_flags, &rx_type, &rx_seq,
                                   &rx_len, gRxBuf);
    WH_TEST_ASSERT_RETURN(rc == WH_ERROR_ABORTED);
    whTest_MeiTeardown();

    /* Runt message (shorter than the fragment header) */
    WH_TEST_RETURN_ON_FAIL(whTest_MeiSetup(512));
    bogus[0] = IMT_FRAG_TAG;
    WH_TEST_RETURN_ON_FAIL(intelMeiSim_InjectRaw(&gClientSim, bogus, 2));
    rc = wh_CommServer_RecvRequest(&gServer, &rx_flags, &rx_type, &rx_seq,
                                   &rx_len, gRxBuf);
    WH_TEST_ASSERT_RETURN(rc == WH_ERROR_ABORTED);
    whTest_MeiTeardown();

    /* First fragment with a nonzero offset */
    WH_TEST_RETURN_ON_FAIL(whTest_MeiSetup(512));
    bogus[0] = IMT_FRAG_TAG;
    bogus[1] = IMT_FRAG_FINAL | 0x01;
    bogus[2] = 0x10; /* offset 16 with no packet in progress */
    bogus[3] = 0x00;
    WH_TEST_RETURN_ON_FAIL(intelMeiSim_InjectRaw(&gClientSim, bogus, 8));
    rc = wh_CommServer_RecvRequest(&gServer, &rx_flags, &rx_type, &rx_seq,
                                   &rx_len, gRxBuf);
    WH_TEST_ASSERT_RETURN(rc == WH_ERROR_ABORTED);
    whTest_MeiTeardown();

    /* Fatal device error mid-exchange */
    WH_TEST_RETURN_ON_FAIL(whTest_MeiSetup(512));
    gClientSim.failNext = 1;
    rc = whTest_MeiEcho(32);
    WH_TEST_ASSERT_RETURN(rc == WH_ERROR_ABORTED);
    whTest_MeiTeardown();

    return 0;
}

/* Full wolfHSM acceptance over the MEI sim at the smallest expected HECI
 * MTU: a complete server (ramsim-flash NVM, crypto) on one thread, the
 * client/server and crypto client test batteries on the other, every
 * message fragmented by the transport. */

#define WH_TEST_MEI_FLASH_RAM_SIZE (1024 * 1024)

static uint8_t gFlashMem[WH_TEST_MEI_FLASH_RAM_SIZE];

typedef int (*whTestMeiClientFn)(whClientConfig* cf);

typedef struct {
    whTestMeiClientFn fn;
    whClientConfig*   cf;
} whTestMeiClientArgs;

/* Set by the client task when its battery completes; the server loop keeps
 * serving (re-arming across client init/cleanup cycles, which the crypto
 * battery performs several times) until then. */
static volatile int gMeiClientDone = 0;

static void* whTest_MeiClientTask(void* arg)
{
    whTestMeiClientArgs* a  = (whTestMeiClientArgs*)arg;
    int                  rc = a->fn(a->cf);
    gMeiClientDone           = 1;
    WH_TEST_ASSERT(0 == rc);
    return NULL;
}

static void* whTest_MeiServerTask(void* cf)
{
    static whServerContext server[1];
    whCommConnected        connected = WH_COMM_CONNECTED;
    int                    ret;

    memset(server, 0, sizeof(server));
    WH_TEST_ASSERT(0 == wh_Server_Init(server, (whServerConfig*)cf));
    WH_TEST_ASSERT(0 == wh_Server_SetConnected(server, WH_COMM_CONNECTED));

    while (gMeiClientDone == 0) {
        ret = wh_Server_HandleRequestMessage(server);
        if ((ret != WH_ERROR_NOTREADY) && (ret != WH_ERROR_OK)) {
            WH_ERROR_PRINT("[mei server] HandleRequestMessage ret=%d\n", ret);
            break;
        }
        /* A client comm-close drops connected; re-arm so the client's next
         * init/cleanup cycle is served by this same loop */
        (void)wh_Server_GetConnected(server, &connected);
        if (connected != WH_COMM_CONNECTED) {
            (void)wh_Server_SetConnected(server, WH_COMM_CONNECTED);
        }
    }
    WH_TEST_ASSERT(0 == wh_Server_Cleanup(server));
    return NULL;
}

/* One client test battery against a full server over a fresh sim pair */
static int whTest_MeiAcceptance(uint32_t maxMsg, whTestMeiClientFn clientFn)
{
    static intelMeiTransportConfig clientTpCfg;
    static intelMeiTransportConfig serverTpCfg;
    static whTransportClientCb     clientTpCb[1] = {IMT_CLIENT_CB};
    static whTransportServerCb     serverTpCb[1] = {IMT_SERVER_CB};
    static whCommClientConfig      cc_conf[1];
    static whCommServerConfig      cs_conf[1];
    static whClientConfig          c_conf[1];
    static whFlashRamsimCtx        fc[1];
    static whFlashRamsimCfg        fc_conf[1];
    static const whFlashCb         fcb[1] = {WH_FLASH_RAMSIM_CB};
    static whTestNvmBackendUnion   nvm_setup;
    static whNvmConfig             n_conf[1];
    static whNvmContext            nvm[1];
#ifndef WOLFHSM_CFG_NO_CRYPTO
    static whServerCryptoContext   crypto[1];
#endif
    static whServerConfig          s_conf[1];

    pthread_t           cthread;
    pthread_t           sthread;
    void*               retval = NULL;
    int                 rc;
    whTestMeiClientArgs cargs;

    WH_TEST_RETURN_ON_FAIL(
        intelMeiSim_CreatePair(&gClientSim, &gServerSim, maxMsg));

    memset(&clientTpCfg, 0, sizeof(clientTpCfg));
    clientTpCfg.ops        = &intelMeiSimDevOps;
    clientTpCfg.devContext = &gClientSim;

    memset(&serverTpCfg, 0, sizeof(serverTpCfg));
    serverTpCfg.ops        = &intelMeiSimDevOps;
    serverTpCfg.devContext = &gServerSim;

    memset(&gClientCtx, 0, sizeof(gClientCtx));
    memset(&gServerCtx, 0, sizeof(gServerCtx));

    memset(cc_conf, 0, sizeof(cc_conf));
    cc_conf[0].transport_cb      = clientTpCb;
    cc_conf[0].transport_context = &gClientCtx;
    cc_conf[0].transport_config  = &clientTpCfg;
    cc_conf[0].client_id         = WH_TEST_DEFAULT_CLIENT_ID;

    memset(c_conf, 0, sizeof(c_conf));
    c_conf[0].comm = cc_conf;

    memset(cs_conf, 0, sizeof(cs_conf));
    cs_conf[0].transport_cb      = serverTpCb;
    cs_conf[0].transport_context = &gServerCtx;
    cs_conf[0].transport_config  = &serverTpCfg;
    cs_conf[0].server_id         = 124;

    memset(gFlashMem, 0, sizeof(gFlashMem));
    memset(fc, 0, sizeof(fc));
    memset(fc_conf, 0, sizeof(fc_conf));
    fc_conf[0].size       = WH_TEST_MEI_FLASH_RAM_SIZE;
    fc_conf[0].sectorSize = WH_TEST_MEI_FLASH_RAM_SIZE / 2;
    fc_conf[0].pageSize   = 8;
    fc_conf[0].erasedByte = (uint8_t)0;
    fc_conf[0].memory     = gFlashMem;

    memset(&nvm_setup, 0, sizeof(nvm_setup));
    memset(n_conf, 0, sizeof(n_conf));
    memset(nvm, 0, sizeof(nvm));
    WH_TEST_RETURN_ON_FAIL(whTest_NvmCfgBackend(WH_NVM_TEST_BACKEND_FLASH,
                                                &nvm_setup, n_conf, fc_conf,
                                                fc, fcb));

    memset(s_conf, 0, sizeof(s_conf));
    s_conf[0].comm_config = cs_conf;
    s_conf[0].nvm         = nvm;
#ifndef WOLFHSM_CFG_NO_CRYPTO
    memset(crypto, 0, sizeof(crypto));
    crypto[0].devId = INVALID_DEVID;
    s_conf[0].crypto = crypto;
    s_conf[0].devId  = INVALID_DEVID;
#endif

    WH_TEST_RETURN_ON_FAIL(wh_Nvm_Init(nvm, n_conf));
#ifndef WOLFHSM_CFG_NO_CRYPTO
    WH_TEST_RETURN_ON_FAIL(wolfCrypt_Init());
    WH_TEST_RETURN_ON_FAIL(wc_InitRng_ex(crypto[0].rng, NULL,
                                         crypto[0].devId));
#endif

    cargs.fn       = clientFn;
    cargs.cf       = c_conf;
    gMeiClientDone = 0;
    rc = pthread_create(&sthread, NULL, whTest_MeiServerTask, s_conf);
    if (rc == 0) {
        rc = pthread_create(&cthread, NULL, whTest_MeiClientTask, &cargs);
        if (rc == 0) {
            /* The server loop exits on the client's comm close */
            pthread_join(cthread, &retval);
            pthread_join(sthread, &retval);
        }
        else {
            pthread_cancel(sthread);
            pthread_join(sthread, &retval);
        }
    }
    WH_TEST_ASSERT_RETURN(rc == 0);

    wh_Nvm_Cleanup(nvm);
#ifndef WOLFHSM_CFG_NO_CRYPTO
    wc_FreeRng(crypto[0].rng);
    wolfCrypt_Cleanup();
#endif
    intelMeiSim_Destroy(&gClientSim);
    intelMeiSim_Destroy(&gServerSim);
    return 0;
}

int whTest_TransportMei(void)
{
    WH_TEST_PRINT("Testing Intel MEI transport (simulator)...\n");
    WH_TEST_RETURN_ON_FAIL(whTest_MeiFragSweep(512));
    WH_TEST_RETURN_ON_FAIL(whTest_MeiFragSweep(1024));
    WH_TEST_RETURN_ON_FAIL(whTest_MeiFragSweep(4096));
    WH_TEST_RETURN_ON_FAIL(whTest_MeiFaults());
    WH_TEST_PRINT("Testing client/server battery over MEI sim @512B...\n");
    WH_TEST_RETURN_ON_FAIL(
        whTest_MeiAcceptance(512, whTest_ClientServerClientConfig));
#ifndef WOLFHSM_CFG_NO_CRYPTO
    WH_TEST_PRINT("Testing crypto battery over MEI sim @512B...\n");
    WH_TEST_RETURN_ON_FAIL(whTest_MeiAcceptance(512, whTest_CryptoClientConfig));
#ifdef WOLFHSM_CFG_KEYWRAP
    WH_TEST_PRINT("Testing keywrap battery over MEI sim @512B...\n");
    WH_TEST_RETURN_ON_FAIL(
        whTest_MeiAcceptance(512, whTest_KeyWrapClientConfig));
#endif
#endif
    WH_TEST_PRINT("MEI TRANSPORT TESTS PASSED\n");
    return 0;
}

#else /* WOLFHSM_CFG_TEST_POSIX && CLIENT && SERVER */

int whTest_TransportMei(void)
{
    return 0;
}

#endif /* WOLFHSM_CFG_TEST_POSIX && WOLFHSM_CFG_ENABLE_CLIENT &&
        * WOLFHSM_CFG_ENABLE_SERVER */
