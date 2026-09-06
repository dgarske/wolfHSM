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
 * examples/posix/wh_ipse_demo/wh_ipse_demo.h
 *
 * Shared defaults for the IPSE demo processes.
 */

#ifndef WH_IPSE_DEMO_H_
#define WH_IPSE_DEMO_H_

/* AF_UNIX path standing in for the /dev/mei* channel */
#define WH_IPSE_DEMO_SOCKET_PATH "/tmp/wh_ipse_demo.sock"

/* Simulated HECI per-message limit (smallest expected client MTU) */
#define WH_IPSE_DEMO_MAX_MSG 512

/* Loopback TCP port for the demo TLS 1.3 connection */
#define WH_IPSE_DEMO_TLS_PORT 23572

#endif /* WH_IPSE_DEMO_H_ */
