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
 * port/intel/mei/intel_mei_sim.h
 *
 * MEI simulator device-ops backend: an AF_UNIX datagram socketpair that
 * preserves message boundaries like HECI does, with a configurable
 * per-message limit (default 512 bytes, the smallest expected HECI client
 * MTU) so the fragmentation core is exercised without hardware.  Supports
 * NOTREADY injection for backpressure testing.
 *
 * Usage: intelMeiSim_CreatePair() connects two contexts (one for the
 * client transport, one for the server transport); each side then passes
 * its context and &intelMeiSimDevOps in its intelMeiTransportConfig with
 * devConfig NULL (the pair is pre-connected, Open is a no-op that reports
 * the configured message limit).
 */

#ifndef PORT_INTEL_MEI_INTEL_MEI_SIM_H_
#define PORT_INTEL_MEI_INTEL_MEI_SIM_H_

#include <stdint.h>

#include "port/intel/mei/intel_mei_transport.h"

#define IMS_DEFAULT_MAX_MSG 512

typedef struct {
    uint32_t maxMsg;
    uint32_t notReadyWrites; /* fail the next N WriteMsg calls NOTREADY */
    uint32_t notReadyReads;  /* fail the next N ReadMsg calls NOTREADY */
    int      fd_p1;          /* fd plus 1 so 0 is invalid */
    uint8_t  failNext;       /* force the next op to WH_ERROR_ABORTED */
    uint8_t  WH_PAD[3];
} intelMeiSimContext;

/* Create a connected simulator pair with the given per-message limit
 * (0 -> IMS_DEFAULT_MAX_MSG).  Both contexts are fully initialized. */
int intelMeiSim_CreatePair(intelMeiSimContext* a, intelMeiSimContext* b,
                           uint32_t maxMsg);

/* Inject one raw message into the peer of ctx (as if a foreign/corrupt
 * message arrived), for desync testing. */
int intelMeiSim_InjectRaw(intelMeiSimContext* ctx, const void* buf,
                          uint16_t len);

extern const intelMeiDevOps intelMeiSimDevOps;

#endif /* !PORT_INTEL_MEI_INTEL_MEI_SIM_H_ */
