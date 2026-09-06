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
 * port/intel/ipse/ipse_mailbox_devops.c
 *
 * Device-ops backend for the IPSE engine-side HECI mailbox -- the
 * firmware's view of the channel the host reaches over /dev/mei*.
 *
 * BLOCKED ON INTEL FDK: the engine mailbox API (message send/receive,
 * doorbell/interrupt, MTU discovery) comes from the Intel IPSE firmware
 * development kit, which is not yet available.  Until then every operation
 * reports WH_ERROR_NOTIMPL; the wh_ipse_demo example substitutes the
 * simulator backend so the identical server code runs on POSIX.
 *
 * To bring this up once the FDK lands, implement the five operations
 * against the FDK mailbox API:
 *   Open     - initialize the mailbox, return the per-message limit
 *   Close    - quiesce the mailbox
 *   WriteMsg - send one whole message to the host (nonblocking:
 *              WH_ERROR_NOTREADY on a full outbound queue)
 *   ReadMsg  - receive one whole message from the host (nonblocking:
 *              WH_ERROR_NOTREADY when none is pending)
 *   GetFd    - not applicable on the engine; keep returning NOTIMPL
 */

#include <stddef.h>
#include <stdint.h>

#include "wolfhsm/wh_settings.h"
#include "wolfhsm/wh_error.h"

#include "port/intel/mei/intel_mei_transport.h"
#include "port/intel/ipse/ipse_mailbox_devops.h"

static int ipseMailbox_Open(void* devContext, const void* devConfig,
                            uint32_t* out_maxMsg)
{
    (void)devContext;
    (void)devConfig;
    (void)out_maxMsg;
    return WH_ERROR_NOTIMPL;
}

static int ipseMailbox_Close(void* devContext)
{
    (void)devContext;
    return WH_ERROR_NOTIMPL;
}

static int ipseMailbox_WriteMsg(void* devContext, const void* buf,
                                uint16_t len)
{
    (void)devContext;
    (void)buf;
    (void)len;
    return WH_ERROR_NOTIMPL;
}

static int ipseMailbox_ReadMsg(void* devContext, void* buf, uint16_t bufLen,
                               uint16_t* out_len)
{
    (void)devContext;
    (void)buf;
    (void)bufLen;
    (void)out_len;
    return WH_ERROR_NOTIMPL;
}

static int ipseMailbox_GetFd(void* devContext, int* out_fd)
{
    (void)devContext;
    (void)out_fd;
    return WH_ERROR_NOTIMPL;
}

const intelMeiDevOps ipseMailboxDevOps = {
    .Open     = ipseMailbox_Open,
    .Close    = ipseMailbox_Close,
    .WriteMsg = ipseMailbox_WriteMsg,
    .ReadMsg  = ipseMailbox_ReadMsg,
    .GetFd    = ipseMailbox_GetFd,
};
