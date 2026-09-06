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
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

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

void intelMeiSim_Destroy(intelMeiSimContext* ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->fd_p1 != 0) {
        close(ctx->fd_p1 - 1);
        ctx->fd_p1 = 0;
    }
    if (ctx->listenFd_p1 != 0) {
        close(ctx->listenFd_p1 - 1);
        ctx->listenFd_p1 = 0;
        if (ctx->path != NULL) {
            (void)unlink(ctx->path);
        }
    }
    ctx->path   = NULL;
    ctx->opened = 0;
}

/* Named mode: bind/listen (server) or connect with a short retry (client) */
static int intelMeiSim_OpenNamed(intelMeiSimContext* d,
                                 const intelMeiSimConfig* cf)
{
    struct sockaddr_un addr;
    int                fd;
    int                rc;
    int                tries;
    struct timespec    ts;

    if ((cf->path == NULL) ||
        (strlen(cf->path) >= sizeof(addr.sun_path))) {
        return WH_ERROR_BADARGS;
    }
    d->maxMsg = (cf->maxMsg != 0) ? cf->maxMsg : IMS_DEFAULT_MAX_MSG;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, cf->path, sizeof(addr.sun_path) - 1);

    fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        return WH_ERROR_ABORTED;
    }

    if (cf->isServer != 0) {
        (void)unlink(cf->path);
        rc = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
        if (rc == 0) {
            rc = listen(fd, 1);
        }
        if ((rc != 0) || (intelMeiSim_MakeNonBlocking(fd) != 0)) {
            close(fd);
            return WH_ERROR_ABORTED;
        }
        d->listenFd_p1 = fd + 1;
        d->path        = cf->path;
    }
    else {
        /* Retry briefly so the server process may be started first */
        rc = -1;
        for (tries = 0; tries < 50; tries++) {
            rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
            if (rc == 0) {
                break;
            }
            ts.tv_sec  = 0;
            ts.tv_nsec = 100000000L; /* 100ms */
            (void)nanosleep(&ts, NULL);
        }
        if ((rc != 0) || (intelMeiSim_MakeNonBlocking(fd) != 0)) {
            close(fd);
            return WH_ERROR_ABORTED;
        }
        d->fd_p1 = fd + 1;
    }
    return 0;
}

/* Named-mode server: accept the client lazily; 0 when a data fd is ready */
static int intelMeiSim_Accept(intelMeiSimContext* d)
{
    int fd;

    if (d->fd_p1 != 0) {
        return 0;
    }
    if (d->listenFd_p1 == 0) {
        return WH_ERROR_ABORTED;
    }
    fd = accept(d->listenFd_p1 - 1, NULL, NULL);
    if (fd < 0) {
        switch (errno) {
            case EAGAIN:
            case EINTR:
                return WH_ERROR_NOTREADY;
            default:
                return WH_ERROR_ABORTED;
        }
    }
    if (intelMeiSim_MakeNonBlocking(fd) != 0) {
        close(fd);
        return WH_ERROR_ABORTED;
    }
    d->fd_p1 = fd + 1;
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
    if ((d->fd_p1 == 0) && (d->listenFd_p1 == 0)) {
        /* Not yet an endpoint: named mode needs a config; pair mode is
         * pre-connected via intelMeiSim_CreatePair */
        const intelMeiSimConfig* cf = (const intelMeiSimConfig*)devConfig;
        int                      rc;
        if (cf == NULL) {
            return WH_ERROR_ABORTED;
        }
        rc = intelMeiSim_OpenNamed(d, cf);
        if (rc != 0) {
            return rc;
        }
    }
    d->opened   = 1;
    *out_maxMsg = d->maxMsg;
    return 0;
}

static int intelMeiSim_Close(void* devContext)
{
    intelMeiSimContext* d = (intelMeiSimContext*)devContext;

    if (d == NULL) {
        return WH_ERROR_BADARGS;
    }
    /* Soft close: the endpoint persists so a later Open can reconnect, the
     * way a real /dev/mei client can be reopened.  intelMeiSim_Destroy()
     * releases the descriptor. */
    d->opened = 0;
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
    if (d->opened == 0) {
        return WH_ERROR_ABORTED;
    }
    if (d->fd_p1 == 0) {
        int arc = intelMeiSim_Accept(d);
        if (arc != 0) {
            return arc;
        }
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
    if (d->opened == 0) {
        return WH_ERROR_ABORTED;
    }
    if (d->fd_p1 == 0) {
        int arc = intelMeiSim_Accept(d);
        if (arc != 0) {
            return arc;
        }
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
