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
 * port/intel/mei/intel_mei_sim.c
 *
 * MEI simulator device-ops backend over an AF_UNIX datagram socketpair.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "wolfhsm/wh_settings.h"
#include "wolfhsm/wh_error.h"

#include "port/intel/mei/intel_mei_transport.h"
#include "port/intel/mei/intel_mei_sim.h"

static int intelMeiSim_MakeNonBlocking(int fd)
{
    int rc = fcntl(fd, F_GETFL, 0);
    if (rc == -1) {
        return WH_ERROR_ABORTED;
    }
    rc = fcntl(fd, F_SETFL, rc | O_NONBLOCK);
    if (rc == -1) {
        return WH_ERROR_ABORTED;
    }
    return 0;
}

int intelMeiSim_CreatePair(intelMeiSimContext* a, intelMeiSimContext* b,
                           uint32_t maxMsg)
{
    int fds[2];
    int rc;

    if ((a == NULL) || (b == NULL)) {
        return WH_ERROR_BADARGS;
    }
    memset(a, 0, sizeof(*a));
    memset(b, 0, sizeof(*b));
    if (maxMsg == 0) {
        maxMsg = IMS_DEFAULT_MAX_MSG;
    }
    rc = socketpair(AF_UNIX, SOCK_DGRAM, 0, fds);
    if (rc != 0) {
        return WH_ERROR_ABORTED;
    }
    if ((intelMeiSim_MakeNonBlocking(fds[0]) != 0) ||
        (intelMeiSim_MakeNonBlocking(fds[1]) != 0)) {
        close(fds[0]);
        close(fds[1]);
        return WH_ERROR_ABORTED;
    }
    a->fd_p1  = fds[0] + 1;
    b->fd_p1  = fds[1] + 1;
    a->maxMsg = maxMsg;
    b->maxMsg = maxMsg;
    return 0;
}

int intelMeiSim_InjectRaw(intelMeiSimContext* ctx, const void* buf,
                          uint16_t len)
{
    ssize_t rc;

    if ((ctx == NULL) || (buf == NULL) || (len == 0) || (ctx->fd_p1 == 0)) {
        return WH_ERROR_BADARGS;
    }
    rc = send(ctx->fd_p1 - 1, buf, (size_t)len, MSG_NOSIGNAL);
    if (rc != (ssize_t)len) {
        return WH_ERROR_ABORTED;
    }
    return 0;
}

static int intelMeiSim_Open(void* devContext, const void* devConfig,
                            uint32_t* out_maxMsg)
{
    intelMeiSimContext* d = (intelMeiSimContext*)devContext;

    (void)devConfig;
    if ((d == NULL) || (out_maxMsg == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (d->fd_p1 == 0) {
        /* Not paired; the sim is pre-connected via intelMeiSim_CreatePair */
        return WH_ERROR_ABORTED;
    }
    *out_maxMsg = d->maxMsg;
    return 0;
}

static int intelMeiSim_Close(void* devContext)
{
    intelMeiSimContext* d = (intelMeiSimContext*)devContext;

    if (d == NULL) {
        return WH_ERROR_BADARGS;
    }
    if (d->fd_p1 != 0) {
        close(d->fd_p1 - 1);
        d->fd_p1 = 0;
    }
    return 0;
}

static int intelMeiSim_WriteMsg(void* devContext, const void* buf,
                                uint16_t len)
{
    intelMeiSimContext* d = (intelMeiSimContext*)devContext;
    ssize_t             rc;

    if ((d == NULL) || (buf == NULL) || (len == 0)) {
        return WH_ERROR_BADARGS;
    }
    if (d->fd_p1 == 0) {
        return WH_ERROR_ABORTED;
    }
    if (d->failNext != 0) {
        d->failNext = 0;
        return WH_ERROR_ABORTED;
    }
    if (d->notReadyWrites > 0) {
        d->notReadyWrites--;
        return WH_ERROR_NOTREADY;
    }
    if ((uint32_t)len > d->maxMsg) {
        /* A real MEI client rejects over-MTU writes */
        return WH_ERROR_BADARGS;
    }
    rc = send(d->fd_p1 - 1, buf, (size_t)len, MSG_NOSIGNAL);
    if (rc < 0) {
        switch (errno) {
            case EAGAIN:
            case EINTR:
                return WH_ERROR_NOTREADY;
            default:
                return WH_ERROR_ABORTED;
        }
    }
    if ((size_t)rc != (size_t)len) {
        return WH_ERROR_ABORTED;
    }
    return 0;
}

static int intelMeiSim_ReadMsg(void* devContext, void* buf, uint16_t bufLen,
                               uint16_t* out_len)
{
    intelMeiSimContext* d = (intelMeiSimContext*)devContext;
    ssize_t             rc;

    if ((d == NULL) || (buf == NULL) || (bufLen == 0) || (out_len == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (d->fd_p1 == 0) {
        return WH_ERROR_ABORTED;
    }
    if (d->failNext != 0) {
        d->failNext = 0;
        return WH_ERROR_ABORTED;
    }
    if (d->notReadyReads > 0) {
        d->notReadyReads--;
        return WH_ERROR_NOTREADY;
    }
    rc = recv(d->fd_p1 - 1, buf, (size_t)bufLen, 0);
    if (rc < 0) {
        switch (errno) {
            case EAGAIN:
            case EINTR:
                return WH_ERROR_NOTREADY;
            default:
                return WH_ERROR_ABORTED;
        }
    }
    if (rc == 0) {
        return WH_ERROR_NOTREADY;
    }
    *out_len = (uint16_t)rc;
    return 0;
}

static int intelMeiSim_GetFd(void* devContext, int* out_fd)
{
    intelMeiSimContext* d = (intelMeiSimContext*)devContext;

    if ((d == NULL) || (out_fd == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (d->fd_p1 == 0) {
        return WH_ERROR_NOTREADY;
    }
    *out_fd = d->fd_p1 - 1;
    return 0;
}

const intelMeiDevOps intelMeiSimDevOps = {
    .Open     = intelMeiSim_Open,
    .Close    = intelMeiSim_Close,
    .WriteMsg = intelMeiSim_WriteMsg,
    .ReadMsg  = intelMeiSim_ReadMsg,
    .GetFd    = intelMeiSim_GetFd,
};
