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
 * port/intel/mei/intel_mei_linux.c
 *
 * Linux /dev/mei* device-ops backend.  The MEI uapi structures and ioctl
 * are defined locally so old distributions without <linux/mei.h> still
 * build (the layout is stable kernel ABI).
 */

#ifdef __linux__

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "wolfhsm/wh_settings.h"
#include "wolfhsm/wh_error.h"

#include "port/intel/mei/intel_mei_transport.h"
#include "port/intel/mei/intel_mei_linux.h"

#define IML_DEFAULT_DEV "/dev/mei0"

/* Provisional wolfHSM IPSE service UUID (generated once for this port) */
const uint8_t intelMeiLinuxDefaultUuid[IML_UUID_SIZE] = {
    0x9d, 0x0a, 0x2f, 0x6e, 0x51, 0xb4, 0x4c, 0x8a,
    0x9e, 0x37, 0x02, 0xc5, 0x8f, 0xd1, 0x33, 0x7a};

/* MEI uapi (stable kernel ABI, defined locally) */
typedef struct {
    uint8_t b[IML_UUID_SIZE];
} imlMeiUuid;

typedef struct {
    union {
        imlMeiUuid in_client_uuid;
        struct {
            uint32_t max_msg_length;
            uint8_t  protocol_version;
            uint8_t  reserved[3];
        } out_client_properties;
    } u;
} imlMeiConnectClientData;

#define IML_IOCTL_MEI_CONNECT_CLIENT \
    _IOWR('H', 0x01, imlMeiConnectClientData)

static int intelMeiLinux_Open(void* devContext, const void* devConfig,
                              uint32_t* out_maxMsg)
{
    intelMeiLinuxContext*      d  = (intelMeiLinuxContext*)devContext;
    const intelMeiLinuxConfig* cf = (const intelMeiLinuxConfig*)devConfig;
    imlMeiConnectClientData    data;
    const char*                path = IML_DEFAULT_DEV;
    const uint8_t*             uuid = intelMeiLinuxDefaultUuid;
    int                        fd;
    int                        rc;

    if ((d == NULL) || (out_maxMsg == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (cf != NULL) {
        if (cf->dev_path != NULL) {
            path = cf->dev_path;
        }
        if (cf->client_uuid != NULL) {
            uuid = cf->client_uuid;
        }
    }
    if (d->fd_p1 != 0) {
        /* Already open */
        *out_maxMsg = d->maxMsg;
        return 0;
    }

    fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        return WH_ERROR_ABORTED;
    }
    memset(&data, 0, sizeof(data));
    memcpy(data.u.in_client_uuid.b, uuid, IML_UUID_SIZE);
    rc = ioctl(fd, IML_IOCTL_MEI_CONNECT_CLIENT, &data);
    if (rc != 0) {
        close(fd);
        return WH_ERROR_ABORTED;
    }
    d->maxMsg = data.u.out_client_properties.max_msg_length;
    d->fd_p1  = fd + 1;
    *out_maxMsg = d->maxMsg;
    return 0;
}

static int intelMeiLinux_Close(void* devContext)
{
    intelMeiLinuxContext* d = (intelMeiLinuxContext*)devContext;

    if (d == NULL) {
        return WH_ERROR_BADARGS;
    }
    if (d->fd_p1 != 0) {
        close(d->fd_p1 - 1);
        d->fd_p1 = 0;
    }
    return 0;
}

static int intelMeiLinux_WriteMsg(void* devContext, const void* buf,
                                  uint16_t len)
{
    intelMeiLinuxContext* d = (intelMeiLinuxContext*)devContext;
    ssize_t               rc;

    if ((d == NULL) || (buf == NULL) || (len == 0)) {
        return WH_ERROR_BADARGS;
    }
    if (d->fd_p1 == 0) {
        return WH_ERROR_ABORTED;
    }
    rc = write(d->fd_p1 - 1, buf, (size_t)len);
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
        /* MEI writes whole messages; a partial write is fatal */
        return WH_ERROR_ABORTED;
    }
    return 0;
}

static int intelMeiLinux_ReadMsg(void* devContext, void* buf, uint16_t bufLen,
                                 uint16_t* out_len)
{
    intelMeiLinuxContext* d = (intelMeiLinuxContext*)devContext;
    ssize_t               rc;

    if ((d == NULL) || (buf == NULL) || (bufLen == 0) || (out_len == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (d->fd_p1 == 0) {
        return WH_ERROR_ABORTED;
    }
    rc = read(d->fd_p1 - 1, buf, (size_t)bufLen);
    if (rc < 0) {
        switch (errno) {
            case EAGAIN:
            case EINTR:
                return WH_ERROR_NOTREADY;
            default:
                /* ENODEV/EBADF etc: engine firmware reset or link loss */
                return WH_ERROR_ABORTED;
        }
    }
    if (rc == 0) {
        return WH_ERROR_NOTREADY;
    }
    *out_len = (uint16_t)rc;
    return 0;
}

static int intelMeiLinux_GetFd(void* devContext, int* out_fd)
{
    intelMeiLinuxContext* d = (intelMeiLinuxContext*)devContext;

    if ((d == NULL) || (out_fd == NULL)) {
        return WH_ERROR_BADARGS;
    }
    if (d->fd_p1 == 0) {
        return WH_ERROR_NOTREADY;
    }
    *out_fd = d->fd_p1 - 1;
    return 0;
}

const intelMeiDevOps intelMeiLinuxDevOps = {
    .Open     = intelMeiLinux_Open,
    .Close    = intelMeiLinux_Close,
    .WriteMsg = intelMeiLinux_WriteMsg,
    .ReadMsg  = intelMeiLinux_ReadMsg,
    .GetFd    = intelMeiLinux_GetFd,
};

#endif /* __linux__ */
