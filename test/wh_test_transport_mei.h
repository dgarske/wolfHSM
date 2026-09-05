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
 * test/wh_test_transport_mei.h
 *
 */

#ifndef TEST_WH_TEST_TRANSPORT_MEI_H_
#define TEST_WH_TEST_TRANSPORT_MEI_H_

/* Runs the Intel MEI transport fragmentation tests over the in-process
 * simulator at several message-size limits, including backpressure and
 * desync fault injection.
 * Returns 0 on success and a non-zero error code on failure */
int whTest_TransportMei(void);

#endif /* TEST_WH_TEST_TRANSPORT_MEI_H_ */
