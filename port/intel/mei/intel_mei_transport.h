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
 * port/intel/mei/intel_mei_transport.h
 *
 * wolfHSM transport over Intel MEI/HECI-style message devices.
 *
 * The Intel Partner Security Engine (IPSE, Panther Lake and later SoCs) is
 * reached from the host over HECI: a message-based channel where each
 * read/write carries one whole message bounded by a per-client MTU
 * (commonly 512-4096 bytes).  wolfHSM's comm layer requires transports to
 * deliver complete packets up to WH_COMM_MTU, which is usually larger than
 * the device MTU, so this transport fragments packets across device
 * messages and reassembles them on the far side.  The fragmentation core is
 * device-independent: a small device-ops vtable (intelMeiDevOps) supplies
 * the actual message I/O, with backends for the Linux /dev/mei* character
 * device (intel_mei_linux.h, host side), an in-process simulator
 * (intel_mei_sim.h, development and CI), and later the IPSE firmware
 * mailbox from the Intel FDK.
 *
 * All calls are non-blocking per the wolfHSM transport contract:
 * WH_ERROR_NOTREADY means retry, WH_ERROR_ABORTED means cleanup.  One
 * request is in flight at a time (enforced by the comm layer); fragments of
 * one packet are delivered in order by the device.
 *
 * Each device message is a 4-byte fragment header followed by payload:
 *   tag(1)       IMT_FRAG_TAG, guards against desync / foreign traffic
 *   seq_flags(1) bits 0-6: packet sequence (mod 128); bit 7: final fragment
 *   offset(2)    little-endian payload offset within the reassembled packet
 */

#ifndef PORT_INTEL_MEI_INTEL_MEI_TRANSPORT_H_
#define PORT_INTEL_MEI_INTEL_MEI_TRANSPORT_H_

#include <stdint.h>

#include "wolfhsm/wh_comm.h"

/* Fragment header */
#define IMT_FRAG_HDR_SIZE 4
#define IMT_FRAG_TAG 0x77u   /* 'w' */
#define IMT_FRAG_FINAL 0x80u /* seq_flags bit 7 */
#define IMT_FRAG_SEQ_MASK 0x7Fu

/* Whole reassembled packet limit (comm header + data) */
#define IMT_PACKET_MAX_SIZE WH_COMM_MTU

/* Largest single device message this transport stages.  A device reporting
 * a bigger MTU is clamped to this. */
#ifndef IMT_MSG_MAX_SIZE
#define IMT_MSG_MAX_SIZE 4096
#endif

/* Smallest usable device MTU (fragment header plus some payload) */
#define IMT_MSG_MIN_SIZE 16

/** Device operations vtable: one whole device message per Write/Read call.
 * All ops return 0 on success, WH_ERROR_NOTREADY if the call would block
 * (retry later), or WH_ERROR_ABORTED on a fatal device error. */
typedef struct {
    /* Open the device; on success *out_maxMsg is the device's per-message
     * limit in bytes (0 if the backend does not know). */
    int (*Open)(void* devContext, const void* devConfig, uint32_t* out_maxMsg);
    int (*Close)(void* devContext);
    /* Write one whole message of len bytes */
    int (*WriteMsg)(void* devContext, const void* buf, uint16_t len);
    /* Read one whole message into buf (bufLen must be >= the device MTU so
     * nothing truncates); *out_len is the received message length */
    int (*ReadMsg)(void* devContext, void* buf, uint16_t bufLen,
                   uint16_t* out_len);
    /* Optional (may be NULL): fd for poll/select integration */
    int (*GetFd)(void* devContext, int* out_fd);
} intelMeiDevOps;

/** Common configuration (both client and server sides) */
typedef struct {
    const intelMeiDevOps* ops;    /* device backend */
    void*                 devContext; /* backend context storage (app owned) */
    const void*           devConfig;  /* backend configuration */
    uint16_t              maxMsg; /* per-message limit override; 0 = use the
                                   * backend-discovered value */
    uint8_t               WH_PAD[6]; /* pads to pointer alignment on both
                                      * 32- and 64-bit targets */
} intelMeiTransportConfig;

/** Shared transport context (used by both the client and server bindings) */
typedef struct {
    const intelMeiDevOps* ops;
    void*                 devContext;
    whCommSetConnectedCb  connectcb;
    void*                 connectcb_arg;
    uint32_t              maxMsg;     /* effective device message limit */
    uint16_t              sendSize;   /* staged packet total; 0 = idle */
    uint16_t              sendOffset; /* bytes already written */
    uint16_t              recvOffset; /* bytes reassembled so far */
    uint8_t               sendSeq;    /* packet sequence for sends (7-bit) */
    uint8_t               recvSeq;    /* latched sequence for the packet
                                       * being reassembled */
    uint8_t               recvActive; /* reassembly in progress */
    uint8_t               connected;
    uint8_t               WH_PAD[2];
    uint8_t               sendBuf[IMT_PACKET_MAX_SIZE];
    uint8_t               recvBuf[IMT_PACKET_MAX_SIZE];
    uint8_t               msgBuf[IMT_MSG_MAX_SIZE];
} intelMeiTransportContext;

/** Client binding */
int intelMeiTransport_InitConnect(void* context, const void* config,
                                  whCommSetConnectedCb connectcb,
                                  void*                connectcb_arg);
int intelMeiTransport_SendRequest(void* context, uint16_t size,
                                  const void* data);
int intelMeiTransport_RecvResponse(void* context, uint16_t* out_size,
                                   void* data);
int intelMeiTransport_CleanupConnect(void* context);

#define IMT_CLIENT_CB                              \
    {                                              \
        .Init    = intelMeiTransport_InitConnect,  \
        .Send    = intelMeiTransport_SendRequest,  \
        .Recv    = intelMeiTransport_RecvResponse, \
        .Cleanup = intelMeiTransport_CleanupConnect,\
    }

/** Server binding */
int intelMeiTransport_InitListen(void* context, const void* config,
                                 whCommSetConnectedCb connectcb,
                                 void*                connectcb_arg);
int intelMeiTransport_RecvRequest(void* context, uint16_t* out_size,
                                  void* data);
int intelMeiTransport_SendResponse(void* context, uint16_t size,
                                   const void* data);
int intelMeiTransport_CleanupListen(void* context);

#define IMT_SERVER_CB                              \
    {                                              \
        .Init    = intelMeiTransport_InitListen,   \
        .Recv    = intelMeiTransport_RecvRequest,  \
        .Send    = intelMeiTransport_SendResponse, \
        .Cleanup = intelMeiTransport_CleanupListen,\
    }

/* Device fd for poll/select, when the backend provides one */
int intelMeiTransport_GetFd(intelMeiTransportContext* context, int* out_fd);

#endif /* !PORT_INTEL_MEI_INTEL_MEI_TRANSPORT_H_ */
