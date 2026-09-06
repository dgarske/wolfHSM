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
 * port/intel/ipse/ipse_server.c
 *
 * IPSE wolfHSM server main loop.  See ipse_server.h.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "wolfhsm/wh_settings.h"

#ifdef WOLFHSM_CFG_ENABLE_SERVER

#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_server.h"
#include "wolfhsm/wh_nvm.h"
#include "wolfhsm/wh_nvm_flash.h"
#include "wolfhsm/wh_flash_ramsim.h"

#ifndef WOLFHSM_CFG_NO_CRYPTO
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/types.h"
#include "wolfssl/wolfcrypt/random.h"
#endif

#include "port/intel/mei/intel_mei_transport.h"
#include "port/intel/ipse/ipse_server.h"

/* Engine-side singletons: an IPSE firmware image hosts exactly one server */
static uint8_t                  gFlashMemory[WOLFHSM_IPSE_FLASH_SIZE];
static whFlashRamsimCtx         gFlashCtx;
static whNvmFlashContext        gNvmFlashCtx;
static whNvmContext             gNvmCtx;
static intelMeiTransportContext gTransportCtx;
#ifndef WOLFHSM_CFG_NO_CRYPTO
static whServerCryptoContext    gCryptoCtx;
#endif
static whServerContext          gServerCtx;

int wh_IpseServer_Run(const intelMeiDevOps* ops, void* devContext,
                      const void* devConfig, const volatile int* stop)
{
    int             rc;
    whCommConnected connected = WH_COMM_CONNECTED;

    /* Configuration singletons: several are retained by reference for the
     * life of the server, so they must persist beyond this call frame */
    static intelMeiTransportConfig tpConfig;
    static whTransportServerCb     tpCb[1] = {IMT_SERVER_CB};
    static whCommServerConfig      commConfig[1];
    static whFlashRamsimCfg        flashConfig;
    static whNvmFlashConfig        nvmFlashConfig;
    static whNvmCb                 nvmCb[1]   = {WH_NVM_FLASH_CB};
    static const whFlashCb         flashCb[1] = {WH_FLASH_RAMSIM_CB};
    static whNvmConfig             nvmConfig;
    static whServerConfig          serverConfig;

    if ((ops == NULL) || (devContext == NULL)) {
        return WH_ERROR_BADARGS;
    }

    /* Transport: the MEI fragmentation core over the supplied backend */
    memset(&tpConfig, 0, sizeof(tpConfig));
    tpConfig.ops        = ops;
    tpConfig.devContext = devContext;
    tpConfig.devConfig  = devConfig;
    memset(&gTransportCtx, 0, sizeof(gTransportCtx));

    memset(commConfig, 0, sizeof(commConfig));
    commConfig[0].transport_cb      = tpCb;
    commConfig[0].transport_context = &gTransportCtx;
    commConfig[0].transport_config  = &tpConfig;
    commConfig[0].server_id         = 1;

    /* NVM: RAM-simulated flash until the IPSE storage service (FDK) */
    memset(gFlashMemory, 0, sizeof(gFlashMemory));
    memset(&gFlashCtx, 0, sizeof(gFlashCtx));
    memset(&flashConfig, 0, sizeof(flashConfig));
    flashConfig.size       = WOLFHSM_IPSE_FLASH_SIZE;
    flashConfig.sectorSize = WOLFHSM_IPSE_FLASH_SIZE / 2;
    flashConfig.pageSize   = 8;
    flashConfig.erasedByte = (uint8_t)0;
    flashConfig.memory     = gFlashMemory;

    memset(&gNvmFlashCtx, 0, sizeof(gNvmFlashCtx));
    memset(&nvmFlashConfig, 0, sizeof(nvmFlashConfig));
    nvmFlashConfig.cb      = flashCb;
    nvmFlashConfig.context = &gFlashCtx;
    nvmFlashConfig.config  = &flashConfig;

    memset(&gNvmCtx, 0, sizeof(gNvmCtx));
    memset(&nvmConfig, 0, sizeof(nvmConfig));
    nvmConfig.cb      = nvmCb;
    nvmConfig.context = &gNvmFlashCtx;
    nvmConfig.config  = &nvmFlashConfig;
    rc = wh_Nvm_Init(&gNvmCtx, &nvmConfig);
    if (rc != 0) {
        return rc;
    }

    memset(&serverConfig, 0, sizeof(serverConfig));
    serverConfig.comm_config = commConfig;
    serverConfig.nvm         = &gNvmCtx;

#ifndef WOLFHSM_CFG_NO_CRYPTO
    /* Server-side wolfCrypt.  crypto.devId stays INVALID_DEVID until the
     * IPSE's own accelerators are exposed through a crypto callback. */
    memset(&gCryptoCtx, 0, sizeof(gCryptoCtx));
    gCryptoCtx.devId = INVALID_DEVID;
    rc               = wolfCrypt_Init();
    if (rc != 0) {
        (void)wh_Nvm_Cleanup(&gNvmCtx);
        return WH_ERROR_ABORTED;
    }
    rc = wc_InitRng_ex(gCryptoCtx.rng, NULL, gCryptoCtx.devId);
    if (rc != 0) {
        (void)wolfCrypt_Cleanup();
        (void)wh_Nvm_Cleanup(&gNvmCtx);
        return WH_ERROR_ABORTED;
    }
    serverConfig.crypto = &gCryptoCtx;
    serverConfig.devId  = INVALID_DEVID;
#endif

    memset(&gServerCtx, 0, sizeof(gServerCtx));
    rc = wh_Server_Init(&gServerCtx, &serverConfig);
    if (rc == 0) {
        rc = wh_Server_SetConnected(&gServerCtx, WH_COMM_CONNECTED);
    }

    while ((rc == 0) && ((stop == NULL) || (*stop == 0))) {
        rc = wh_Server_HandleRequestMessage(&gServerCtx);
        if (rc == WH_ERROR_NOTREADY) {
            rc = 0;
            WOLFHSM_IPSE_YIELD();
        }
        else if (rc != WH_ERROR_OK) {
            break;
        }
        /* A client comm-close drops connected; re-arm for the next client */
        (void)wh_Server_GetConnected(&gServerCtx, &connected);
        if (connected != WH_COMM_CONNECTED) {
            (void)wh_Server_SetConnected(&gServerCtx, WH_COMM_CONNECTED);
        }
    }

    (void)wh_Server_Cleanup(&gServerCtx);
#ifndef WOLFHSM_CFG_NO_CRYPTO
    wc_FreeRng(gCryptoCtx.rng);
    (void)wolfCrypt_Cleanup();
#endif
    (void)wh_Nvm_Cleanup(&gNvmCtx);
    return rc;
}

#endif /* WOLFHSM_CFG_ENABLE_SERVER */
