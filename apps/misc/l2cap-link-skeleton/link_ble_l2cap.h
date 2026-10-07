//
// Copyright (c) 2026 teleop-bench
//
// This program and the accompanying materials are made available under the
// terms of the Eclipse Public License 2.0 which is available at
// http://www.eclipse.org/legal/epl-2.0, or the Apache License, Version 2.0
// which is available at https://www.apache.org/licenses/LICENSE-2.0.
//
// SPDX-License-Identifier: EPL-2.0 OR Apache-2.0
//

#ifndef ZENOH_PICO_LINK_TRANSPORT_BLE_L2CAP_H
#define ZENOH_PICO_LINK_TRANSPORT_BLE_L2CAP_H

#include <stddef.h>
#include <stdint.h>

#include "zenoh-pico/config.h"

#if Z_FEATURE_LINK_BLE_L2CAP == 1

#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/system/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Schema & Config Keys
// ---------------------------------------------------------------------------

// Locator schema: "ble/AA:BB:CC:DD:EE:FF#psm=128&mtu=512"
#define BLE_L2CAP_SCHEMA "ble"

// Config key IDs (used in _z_str_intmap)
#define BLE_L2CAP_CONFIG_PSM_KEY       0x01
#define BLE_L2CAP_CONFIG_PSM_STR       "psm"
#define BLE_L2CAP_CONFIG_MTU_KEY       0x02
#define BLE_L2CAP_CONFIG_MTU_STR       "mtu"
#define BLE_L2CAP_CONFIG_ADDR_TYPE_KEY 0x03
#define BLE_L2CAP_CONFIG_ADDR_TYPE_STR "addr_type"
#define BLE_L2CAP_CONFIG_TOUT_KEY      0x04
#define BLE_L2CAP_CONFIG_TOUT_STR      "tout"

// Defaults
#define BLE_L2CAP_DEFAULT_PSM          0x0080   // First dynamic LE PSM
#define BLE_L2CAP_DEFAULT_MTU          512      // ESP32 max practical SDU
#define BLE_L2CAP_DEFAULT_TOUT_MS      10000

// Address types
#define _Z_BLE_ADDR_TYPE_PUBLIC  0
#define _Z_BLE_ADDR_TYPE_RANDOM  1

// ---------------------------------------------------------------------------
// Socket Structure
// ---------------------------------------------------------------------------

typedef struct {
    _z_sys_net_socket_t _sock;   // Platform socket (fd on Linux, opaque on ESP32)
    char *_remote_addr;          // Remote BLE address string (heap-allocated)
    uint16_t _psm;               // Negotiated PSM
    uint16_t _mtu;               // Negotiated MTU (min of our/peer)
} _z_ble_l2cap_socket_t;

// ---------------------------------------------------------------------------
// Platform-Specific Low-Level API
// ---------------------------------------------------------------------------
// Each platform (ESP-IDF, Linux, macOS) implements these functions.
// They are called by the link manager layer.

/**
 * Open an L2CAP CoC connection to a remote BLE device (client/central role).
 *
 * @param sock      Output socket structure to fill
 * @param addr      Remote BLE address string "AA:BB:CC:DD:EE:FF"
 * @param addr_type Address type (public=0, random=1)
 * @param psm       Protocol/Service Multiplexer
 * @param mtu       Requested MTU (SDU size)
 * @param tout_ms   Connection timeout in milliseconds
 * @return _Z_RES_OK on success
 */
z_result_t _z_open_ble_l2cap(_z_ble_l2cap_socket_t *sock, const char *addr,
                              uint8_t addr_type, uint16_t psm, uint16_t mtu,
                              uint32_t tout_ms);

/**
 * Start listening for L2CAP CoC connections (server/peripheral role).
 *
 * @param sock      Output socket structure to fill (with accepted client)
 * @param psm       PSM to listen on
 * @param mtu       Advertised MTU
 * @param tout_ms   Accept timeout in milliseconds
 * @return _Z_RES_OK on success
 */
z_result_t _z_listen_ble_l2cap(_z_ble_l2cap_socket_t *sock, uint16_t psm,
                                uint16_t mtu, uint32_t tout_ms);

/**
 * Close an L2CAP CoC connection.
 */
void _z_close_ble_l2cap(_z_ble_l2cap_socket_t *sock);

/**
 * Read data from an L2CAP CoC channel (blocking).
 *
 * @param sock  Socket to read from
 * @param ptr   Buffer to read into
 * @param len   Maximum bytes to read
 * @return Number of bytes read, or SIZE_MAX on error/disconnect
 */
size_t _z_read_ble_l2cap(const _z_ble_l2cap_socket_t sock, uint8_t *ptr, size_t len);

/**
 * Send data over L2CAP CoC channel.
 *
 * @return Number of bytes sent, or SIZE_MAX on error
 */
size_t _z_send_ble_l2cap(const _z_ble_l2cap_socket_t sock, const uint8_t *ptr, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* Z_FEATURE_LINK_BLE_L2CAP == 1 */

#endif /* ZENOH_PICO_LINK_TRANSPORT_BLE_L2CAP_H */
