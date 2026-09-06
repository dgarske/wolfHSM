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
 * examples/posix/wh_ipse_demo/wolfhsm_cfg.h
 *
 * wolfHSM configuration for the IPSE demo: host client and simulated
 * engine server over the Intel MEI transport.
 */

#ifndef WOLFHSM_CFG_H_
#define WOLFHSM_CFG_H_

#define WOLFHSM_CFG_ENABLE_CLIENT
#define WOLFHSM_CFG_ENABLE_SERVER

/* POSIX time source (the engine build supplies its own or defines
 * WOLFHSM_CFG_NO_SYS_TIME) */
#define WOLFHSM_CFG_PORT_GETTIME posixGetTime

/* Comm payload sized so ML-DSA-87 signatures (4627B) fit in one packet;
 * the MEI transport fragments packets to the device MTU underneath */
#define WOLFHSM_CFG_COMM_DATA_LEN 5120

#define WOLFHSM_CFG_NVM_OBJECT_COUNT 30
#define WOLFHSM_CFG_SERVER_KEYCACHE_COUNT 9
#define WOLFHSM_CFG_SERVER_KEYCACHE_BUFSIZE 300
#define WOLFHSM_CFG_SERVER_KEYCACHE_BIG_COUNT 3
#define WOLFHSM_CFG_SERVER_KEYCACHE_BIG_BUFSIZE WOLFHSM_CFG_COMM_DATA_LEN
#define WOLFHSM_CFG_SERVER_CUSTOMCB_COUNT 2
#define WOLFHSM_CFG_DMAADDR_COUNT 4

/* Demo: idle IPSE server sleeps ~1ms per empty poll instead of busy-spinning a
 * core. Real targets override WOLFHSM_IPSE_YIELD with a wait-for-event hook. */
#include <time.h>
#define WOLFHSM_IPSE_YIELD()                     \
    do {                                         \
        struct timespec ipseYieldTs;             \
        ipseYieldTs.tv_sec  = 0;                 \
        ipseYieldTs.tv_nsec = 1000000L;          \
        (void)nanosleep(&ipseYieldTs, NULL);     \
    } while (0)

#endif /* WOLFHSM_CFG_H_ */
