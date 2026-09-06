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
 * port/intel/ipse/ipse_server.h
 *
 * wolfHSM server skeleton for the Intel Partner Security Engine (IPSE):
 * the main loop intended to run inside the engine as wolfSSL partner
 * firmware.  It assembles a complete wolfHSM server (RAM-simulated flash
 * NVM until the IPSE storage service is available, server-side wolfCrypt,
 * and the Intel MEI transport) over whatever device backend is supplied:
 * the in-process/named simulator today, the Intel FDK mailbox
 * (ipse_mailbox_devops) once the FDK is available.
 *
 * Pre-FDK the same code compiles and runs on POSIX, which is how the
 * wh_ipse_demo example uses it.
 */

#ifndef PORT_INTEL_IPSE_IPSE_SERVER_H_
#define PORT_INTEL_IPSE_IPSE_SERVER_H_

#include <stdint.h>

#include "port/intel/mei/intel_mei_transport.h"

#ifdef WOLFHSM_CFG_ENABLE_SERVER

/* Size of the RAM-simulated NVM flash backing the key store.  Replaced by
 * the IPSE dedicated SPI-NOR region when the FDK storage service lands. */
#ifndef WOLFHSM_IPSE_FLASH_SIZE
#define WOLFHSM_IPSE_FLASH_SIZE (128 * 1024)
#endif

/* Hook run once per idle loop iteration (no request pending).  On an RTOS
 * or the engine, point this at a yield/wait-for-event primitive. */
#ifndef WOLFHSM_IPSE_YIELD
#define WOLFHSM_IPSE_YIELD() \
    do {                     \
    } while (0)
#endif

/* Run the IPSE wolfHSM server over the given MEI device backend.  Serves
 * clients until *stop becomes nonzero (stop == NULL runs forever).  Client
 * disconnects re-arm the server for the next connection.  Returns 0 on a
 * requested stop, or a negative wolfHSM error on a fatal failure. */
int wh_IpseServer_Run(const intelMeiDevOps* ops, void* devContext,
                      const void* devConfig, const volatile int* stop);

#endif /* WOLFHSM_CFG_ENABLE_SERVER */

#endif /* !PORT_INTEL_IPSE_IPSE_SERVER_H_ */
