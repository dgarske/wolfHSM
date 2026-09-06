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
 * examples/posix/wh_ipse_demo/user_settings.h
 *
 * wolfSSL configuration for the IPSE demo: TLS 1.3 on the host client with
 * crypto routed through the wolfHSM crypto callback (WH_DEV_ID), wolfCrypt
 * serving on the simulated engine side.
 */

#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

/** wolfHSM required settings for wolfCrypt */
#define WOLF_CRYPTO_CB
#define WOLFSSL_KEY_GEN
#define WOLFSSL_ASN_TEMPLATE
#define WOLFSSL_BASE64_ENCODE
#define HAVE_ANONYMOUS_INLINE_AGGREGATES 1
#define NO_INLINE
#define WOLFSSL_IGNORE_FILE_WARN

/** Math and hardening */
#define USE_FAST_MATH
#define FP_MAX_BITS 8192
#define WOLFSSL_USE_ALIGN
#define TFM_TIMING_RESISTANT
#define ECC_TIMING_RESISTANT
#define WC_RSA_BLINDING

/** TLS 1.3 */
#define WOLFSSL_TLS13
#define HAVE_TLS_EXTENSIONS
#define HAVE_SUPPORTED_CURVES
#define HAVE_HKDF
#define WC_RSA_PSS
#define NO_OLD_TLS
#define WOLFSSL_NO_TLS12

/** Crypto for the TLS 1.3 ECDHE-ECDSA-AES-GCM demo profile */
#define HAVE_ECC
#define TFM_ECC256
#define ECC_SHAMIR
#define HAVE_AESGCM
#define GCM_TABLE_4BIT
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512
#define WOLFSSL_CMAC
#define WOLFSSL_AES_DIRECT
#define HAVE_AES_ECB
#define HAVE_CURVE25519

/** Demo certificates (compiled-in buffers, no filesystem paths needed) */
#define USE_CERT_BUFFERS_256
#define USE_CERT_BUFFERS_2048

/* Prevents non-devId server-side wolfCrypt calls from falling back into the
 * client crypto callback */
#define WC_NO_DEFAULT_DEVID

/** Trim */
#define NO_PSK
#define NO_DH
#define NO_DSA
#define NO_RC4
#define NO_MD4
#define NO_MD5
#define NO_SHA
#define NO_DES3
#define NO_PWDBASED
#define NO_OLD_RNGNAME
#define NO_OLD_WC_NAMES
#define NO_OLD_SSL_NAMES
#define NO_OLD_SHA_NAMES
#define NO_ERROR_QUEUE

/* POSIX version of strcasecmp */
#include <strings.h>

#endif /* USER_SETTINGS_H */
