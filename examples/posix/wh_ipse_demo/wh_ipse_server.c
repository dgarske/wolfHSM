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
 * examples/posix/wh_ipse_demo/wh_ipse_server.c
 *
 * The "IPSE firmware" process: runs the port/intel/ipse wolfHSM server
 * skeleton over the MEI simulator's named socket (standing in for the
 * engine-side HECI mailbox until the Intel FDK is available).
 *
 * Usage: wh_ipse_server [socket_path] [max_msg]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "wolfhsm/wh_error.h"

#include "port/intel/mei/intel_mei_sim.h"
#include "port/intel/ipse/ipse_server.h"

#include "wh_ipse_demo.h"

static volatile int gStop = 0;

static void _sigHandler(int sig)
{
    (void)sig;
    gStop = 1;
}

int main(int argc, char** argv)
{
    int                rc;
    intelMeiSimContext simCtx;
    intelMeiSimConfig  simConfig;

    memset(&simCtx, 0, sizeof(simCtx));
    memset(&simConfig, 0, sizeof(simConfig));
    simConfig.path     = (argc > 1) ? argv[1] : WH_IPSE_DEMO_SOCKET_PATH;
    simConfig.maxMsg   = (argc > 2) ? (uint32_t)atoi(argv[2])
                                    : WH_IPSE_DEMO_MAX_MSG;
    simConfig.isServer = 1;

    signal(SIGINT, _sigHandler);
    signal(SIGTERM, _sigHandler);

    printf("IPSE demo server: wolfHSM over MEI sim at %s (max msg %u)\n",
           simConfig.path, (unsigned int)simConfig.maxMsg);
    printf("Press Ctrl+C to stop\n");

    rc = wh_IpseServer_Run(&intelMeiSimDevOps, &simCtx, &simConfig, &gStop);

    intelMeiSim_Destroy(&simCtx);
    printf("IPSE demo server exit: %d\n", rc);
    return (rc == 0) ? 0 : 1;
}
