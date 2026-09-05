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
 * port/intel/mei/intel_mei_linux.h
 *
 * Device-ops backend for the Linux MEI character device (/dev/mei*): opens
 * the device, connects to the HECI client UUID, and exchanges whole
 * messages.  The connect ioctl reply provides the client's max message
 * length, surfaced to the fragmentation core as the device MTU.
 */

#ifndef PORT_INTEL_MEI_INTEL_MEI_LINUX_H_
#define PORT_INTEL_MEI_INTEL_MEI_LINUX_H_

#include <stdint.h>

#include "port/intel/mei/intel_mei_transport.h"

/* Provisional HECI client UUID for the wolfHSM IPSE service, used when the
 * config does not supply one.  Replace when Intel assigns an official
 * client UUID for the wolfSSL partner firmware. */
#define IML_UUID_SIZE 16
extern const uint8_t intelMeiLinuxDefaultUuid[IML_UUID_SIZE];

typedef struct {
    const char*    dev_path;    /* NULL -> "/dev/mei0" */
    const uint8_t* client_uuid; /* 16 bytes; NULL -> provisional default */
} intelMeiLinuxConfig;

typedef struct {
    uint32_t maxMsg;
    int      fd_p1; /* fd plus 1 so 0 is invalid */
} intelMeiLinuxContext;

/* The intelMeiDevOps instance for this backend */
extern const intelMeiDevOps intelMeiLinuxDevOps;

#endif /* !PORT_INTEL_MEI_INTEL_MEI_LINUX_H_ */
