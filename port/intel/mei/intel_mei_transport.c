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
 * port/intel/mei/intel_mei_transport.c
 *
 * Fragmentation core of the Intel MEI/HECI wolfHSM transport.  Splits
 * wolfHSM comm packets into device messages on Send and reassembles them on
 * Recv, over the device-ops vtable declared in intel_mei_transport.h.  The
 * client and server bindings are the same state machine; only the calling
 * side differs.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "wolfhsm/wh_settings.h"
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_comm.h"

#include "port/intel/mei/intel_mei_transport.h"

/** Local declarations */
static int intelMeiTransport_Init(intelMeiTransportContext* c,
                                  const intelMeiTransportConfig* cf,
                                  whCommSetConnectedCb connectcb,
                                  void* connectcb_arg);
static int intelMeiTransport_Send(intelMeiTransportContext* c, uint16_t size,
                                  const void* data);
static int intelMeiTransport_Recv(intelMeiTransportContext* c,
                                  uint16_t* out_size, void* data);
static int intelMeiTransport_Cleanup(intelMeiTransportContext* c);
static void intelMeiTransport_Disconnect(intelMeiTransportContext* c);

/** Local implementations */

static void intelMeiTransport_Disconnect(intelMeiTransportContext* c)
{
    c->sendSize   = 0;
    c->sendOffset = 0;
    c->recvOffset = 0;
    c->recvActive = 0;
    if (c->connected) {
        c->connected = 0;
        if (c->connectcb != NULL) {
            c->connectcb(c->connectcb_arg, WH_COMM_DISCONNECTED);
        }
    }
}

static int intelMeiTransport_Init(intelMeiTransportContext* c,
                                  const intelMeiTransportConfig* cf,
                                  whCommSetConnectedCb connectcb,
                                  void* connectcb_arg)
{
    int      rc;
    uint32_t devMax = 0;

    if ((c == NULL) || (cf == NULL) || (cf->ops == NULL) ||
        (cf->ops->Open == NULL) || (cf->ops->Close == NULL) ||
        (cf->ops->WriteMsg == NULL) || (cf->ops->ReadMsg == NULL)) {
        return WH_ERROR_BADARGS;
    }

    memset(c, 0, sizeof(*c));
    c->ops        = cf->ops;
    c->devContext = cf->devContext;

    rc = c->ops->Open(c->devContext, cf->devConfig, &devMax);
    if (rc != 0) {
        return rc;
    }

    /* Effective message limit: the smaller of the discovered device limit,
     * the configured override, and this transport's staging buffer */
    c->maxMsg = IMT_MSG_MAX_SIZE;
    if ((devMax != 0) && (devMax < c->maxMsg)) {
        c->maxMsg = devMax;
    }
    if ((cf->maxMsg != 0) && ((uint32_t)cf->maxMsg < c->maxMsg)) {
        c->maxMsg = cf->maxMsg;
    }
    if (c->maxMsg < IMT_MSG_MIN_SIZE) {
        (void)c->ops->Close(c->devContext);
        return WH_ERROR_BADARGS;
    }

    c->connectcb     = connectcb;
    c->connectcb_arg = connectcb_arg;
    c->connected     = 1;
    if (c->connectcb != NULL) {
        c->connectcb(c->connectcb_arg, WH_COMM_CONNECTED);
    }
    return 0;
}

static int intelMeiTransport_Send(intelMeiTransportContext* c, uint16_t size,
                                  const void* data)
{
    int      rc;
    uint16_t chunk;
    uint16_t chunkMax;
    uint8_t  seqFlags;

    if ((c == NULL) || (size == 0) || (size > IMT_PACKET_MAX_SIZE) ||
        (data == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (c->connected == 0) {
        return WH_ERROR_ABORTED;
    }

    if (c->sendSize == 0) {
        /* New packet: stage it and start a new sequence number */
        memcpy(c->sendBuf, data, size);
        c->sendSize   = size;
        c->sendOffset = 0;
        c->sendSeq    = (uint8_t)((c->sendSeq + 1) & IMT_FRAG_SEQ_MASK);
    }
    /* On retry the caller repeats the same packet; the staged copy is used */

    chunkMax = (uint16_t)(c->maxMsg - IMT_FRAG_HDR_SIZE);
    while (c->sendOffset < c->sendSize) {
        chunk = (uint16_t)(c->sendSize - c->sendOffset);
        if (chunk > chunkMax) {
            chunk = chunkMax;
        }
        seqFlags = c->sendSeq;
        if ((uint16_t)(c->sendOffset + chunk) == c->sendSize) {
            seqFlags |= IMT_FRAG_FINAL;
        }
        c->msgBuf[0] = IMT_FRAG_TAG;
        c->msgBuf[1] = seqFlags;
        c->msgBuf[2] = (uint8_t)(c->sendOffset & 0xFFu);
        c->msgBuf[3] = (uint8_t)((c->sendOffset >> 8) & 0xFFu);
        memcpy(&c->msgBuf[IMT_FRAG_HDR_SIZE], &c->sendBuf[c->sendOffset],
               chunk);

        rc = c->ops->WriteMsg(c->devContext, c->msgBuf,
                              (uint16_t)(IMT_FRAG_HDR_SIZE + chunk));
        if (rc == WH_ERROR_NOTREADY) {
            /* Device backpressure; resume from sendOffset on retry */
            return WH_ERROR_NOTREADY;
        }
        if (rc != 0) {
            intelMeiTransport_Disconnect(c);
            return WH_ERROR_ABORTED;
        }
        c->sendOffset = (uint16_t)(c->sendOffset + chunk);
    }

    /* Whole packet written */
    c->sendSize   = 0;
    c->sendOffset = 0;
    return 0;
}

static int intelMeiTransport_Recv(intelMeiTransportContext* c,
                                  uint16_t* out_size, void* data)
{
    int      rc;
    uint16_t got = 0;
    uint16_t payload;
    uint16_t offset;
    uint8_t  seq;
    uint8_t  final;

    if ((c == NULL) || (out_size == NULL) || (data == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (c->connected == 0) {
        return WH_ERROR_ABORTED;
    }

    for (;;) {
        got = (uint16_t)c->maxMsg;
        rc  = c->ops->ReadMsg(c->devContext, c->msgBuf, got, &got);
        if (rc == WH_ERROR_NOTREADY) {
            /* Nothing more available; reassembly state is retained */
            return WH_ERROR_NOTREADY;
        }
        if (rc != 0) {
            intelMeiTransport_Disconnect(c);
            return WH_ERROR_ABORTED;
        }
        if ((got < IMT_FRAG_HDR_SIZE) || (c->msgBuf[0] != IMT_FRAG_TAG)) {
            /* Runt or foreign message: unrecoverable desync */
            intelMeiTransport_Disconnect(c);
            return WH_ERROR_ABORTED;
        }
        seq    = (uint8_t)(c->msgBuf[1] & IMT_FRAG_SEQ_MASK);
        final  = (uint8_t)(c->msgBuf[1] & IMT_FRAG_FINAL);
        offset = (uint16_t)((uint16_t)c->msgBuf[2] |
                            ((uint16_t)c->msgBuf[3] << 8));
        payload = (uint16_t)(got - IMT_FRAG_HDR_SIZE);

        if (c->recvActive == 0) {
            /* First fragment of a packet must start at offset 0 */
            if (offset != 0) {
                intelMeiTransport_Disconnect(c);
                return WH_ERROR_ABORTED;
            }
            c->recvActive = 1;
            c->recvSeq    = seq;
            c->recvOffset = 0;
        }
        else {
            /* Continuation must match the packet in progress */
            if ((seq != c->recvSeq) || (offset != c->recvOffset)) {
                intelMeiTransport_Disconnect(c);
                return WH_ERROR_ABORTED;
            }
        }
        if ((payload > (uint16_t)(IMT_PACKET_MAX_SIZE - c->recvOffset))) {
            intelMeiTransport_Disconnect(c);
            return WH_ERROR_ABORTED;
        }
        if (payload > 0) {
            memcpy(&c->recvBuf[c->recvOffset], &c->msgBuf[IMT_FRAG_HDR_SIZE],
                   payload);
            c->recvOffset = (uint16_t)(c->recvOffset + payload);
        }

        if (final != 0) {
            *out_size = c->recvOffset;
            memcpy(data, c->recvBuf, c->recvOffset);
            c->recvActive = 0;
            c->recvOffset = 0;
            return 0;
        }
        /* Not final: loop for the next fragment */
    }
}

static int intelMeiTransport_Cleanup(intelMeiTransportContext* c)
{
    if (c == NULL) {
        return WH_ERROR_BADARGS;
    }
    intelMeiTransport_Disconnect(c);
    if (c->ops != NULL) {
        (void)c->ops->Close(c->devContext);
    }
    return 0;
}

/** Client binding */

int intelMeiTransport_InitConnect(void* context, const void* config,
                                  whCommSetConnectedCb connectcb,
                                  void*                connectcb_arg)
{
    return intelMeiTransport_Init((intelMeiTransportContext*)context,
                                  (const intelMeiTransportConfig*)config,
                                  connectcb, connectcb_arg);
}

int intelMeiTransport_SendRequest(void* context, uint16_t size,
                                  const void* data)
{
    return intelMeiTransport_Send((intelMeiTransportContext*)context, size,
                                  data);
}

int intelMeiTransport_RecvResponse(void* context, uint16_t* out_size,
                                   void* data)
{
    return intelMeiTransport_Recv((intelMeiTransportContext*)context,
                                  out_size, data);
}

int intelMeiTransport_CleanupConnect(void* context)
{
    return intelMeiTransport_Cleanup((intelMeiTransportContext*)context);
}

/** Server binding */

int intelMeiTransport_InitListen(void* context, const void* config,
                                 whCommSetConnectedCb connectcb,
                                 void*                connectcb_arg)
{
    return intelMeiTransport_Init((intelMeiTransportContext*)context,
                                  (const intelMeiTransportConfig*)config,
                                  connectcb, connectcb_arg);
}

int intelMeiTransport_RecvRequest(void* context, uint16_t* out_size,
                                  void* data)
{
    return intelMeiTransport_Recv((intelMeiTransportContext*)context,
                                  out_size, data);
}

int intelMeiTransport_SendResponse(void* context, uint16_t size,
                                   const void* data)
{
    return intelMeiTransport_Send((intelMeiTransportContext*)context, size,
                                  data);
}

int intelMeiTransport_CleanupListen(void* context)
{
    return intelMeiTransport_Cleanup((intelMeiTransportContext*)context);
}

int intelMeiTransport_GetFd(intelMeiTransportContext* context, int* out_fd)
{
    if ((context == NULL) || (out_fd == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if ((context->ops == NULL) || (context->ops->GetFd == NULL)) {
        return WH_ERROR_NOHANDLER;
    }
    return context->ops->GetFd(context->devContext, out_fd);
}
