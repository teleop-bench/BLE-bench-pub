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

// This file would live at: src/link/unicast/ble_l2cap.c
// It is the link manager layer -- validates endpoints, wires function pointers,
// and delegates to the platform-specific implementations.

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "zenoh-pico/config.h"

#if Z_FEATURE_LINK_BLE_L2CAP == 1

#include "zenoh-pico/link/manager.h"
#include "zenoh-pico/link/transport/ble_l2cap.h"  // link_ble_l2cap.h
#include "zenoh-pico/utils/logging.h"
#include "zenoh-pico/utils/pointers.h"

// ---------------------------------------------------------------------------
// Endpoint Validation
// ---------------------------------------------------------------------------

z_result_t _z_endpoint_ble_l2cap_valid(_z_endpoint_t *ep) {
    // Check schema is "ble"
    _z_string_t ble_str = _z_string_alias_str(BLE_L2CAP_SCHEMA);
    if (!_z_string_equals(&ep->_locator._protocol, &ble_str)) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
        return _Z_ERR_CONFIG_LOCATOR_INVALID;
    }

    // Address must be present (e.g., "AA:BB:CC:DD:EE:FF" or "*")
    if (_z_string_len(&ep->_locator._address) == (size_t)0) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
        return _Z_ERR_CONFIG_LOCATOR_INVALID;
    }

    return _Z_RES_OK;
}

// ---------------------------------------------------------------------------
// Helper: Convert address string
// ---------------------------------------------------------------------------

static char *_z_ble_l2cap_convert_address(_z_string_t *address) {
    char *ret = (char *)z_malloc(_z_string_len(address) + 1);
    if (ret != NULL) {
        _z_str_n_copy(ret, _z_string_data(address), _z_string_len(address) + 1);
    }
    return ret;
}

// ---------------------------------------------------------------------------
// Helper: Parse config values from endpoint
// ---------------------------------------------------------------------------

static uint16_t _z_ble_l2cap_get_psm(const _z_endpoint_t *ep) {
    char *psm_str = _z_str_intmap_get(&ep->_config, BLE_L2CAP_CONFIG_PSM_KEY);
    if (psm_str != NULL) {
        return (uint16_t)strtoul(psm_str, NULL, 10);
    }
    return BLE_L2CAP_DEFAULT_PSM;
}

static uint16_t _z_ble_l2cap_get_mtu(const _z_endpoint_t *ep) {
    char *mtu_str = _z_str_intmap_get(&ep->_config, BLE_L2CAP_CONFIG_MTU_KEY);
    if (mtu_str != NULL) {
        return (uint16_t)strtoul(mtu_str, NULL, 10);
    }
    return BLE_L2CAP_DEFAULT_MTU;
}

static uint8_t _z_ble_l2cap_get_addr_type(const _z_endpoint_t *ep) {
    char *at_str = _z_str_intmap_get(&ep->_config, BLE_L2CAP_CONFIG_ADDR_TYPE_KEY);
    if (at_str != NULL && strcmp(at_str, "random") == 0) {
        return _Z_BLE_ADDR_TYPE_RANDOM;
    }
    return _Z_BLE_ADDR_TYPE_PUBLIC;
}

static uint32_t _z_ble_l2cap_get_tout(const _z_endpoint_t *ep) {
    char *tout_str = _z_str_intmap_get(&ep->_config, BLE_L2CAP_CONFIG_TOUT_KEY);
    if (tout_str != NULL) {
        return (uint32_t)strtoul(tout_str, NULL, 10);
    }
    return BLE_L2CAP_DEFAULT_TOUT_MS;
}

// ---------------------------------------------------------------------------
// Link Function Pointer Implementations
// ---------------------------------------------------------------------------

z_result_t _z_f_link_open_ble_l2cap(_z_link_t *self) {
    uint16_t psm = _z_ble_l2cap_get_psm(&self->_endpoint);
    uint16_t mtu = _z_ble_l2cap_get_mtu(&self->_endpoint);
    uint8_t addr_type = _z_ble_l2cap_get_addr_type(&self->_endpoint);
    uint32_t tout = _z_ble_l2cap_get_tout(&self->_endpoint);

    self->_socket._ble_l2cap._remote_addr =
        _z_ble_l2cap_convert_address(&self->_endpoint._locator._address);

    return _z_open_ble_l2cap(&self->_socket._ble_l2cap,
                              self->_socket._ble_l2cap._remote_addr,
                              addr_type, psm, mtu, tout);
}

z_result_t _z_f_link_listen_ble_l2cap(_z_link_t *self) {
    uint16_t psm = _z_ble_l2cap_get_psm(&self->_endpoint);
    uint16_t mtu = _z_ble_l2cap_get_mtu(&self->_endpoint);
    uint32_t tout = _z_ble_l2cap_get_tout(&self->_endpoint);

    return _z_listen_ble_l2cap(&self->_socket._ble_l2cap, psm, mtu, tout);
}

void _z_f_link_close_ble_l2cap(_z_link_t *self) {
    _z_close_ble_l2cap(&self->_socket._ble_l2cap);
}

void _z_f_link_free_ble_l2cap(_z_link_t *self) {
    if (self->_socket._ble_l2cap._remote_addr != NULL) {
        z_free(self->_socket._ble_l2cap._remote_addr);
        self->_socket._ble_l2cap._remote_addr = NULL;
    }
}

size_t _z_f_link_write_ble_l2cap(const _z_link_t *self, const uint8_t *ptr,
                                  size_t len, _z_sys_net_socket_t *socket) {
    _ZP_UNUSED(socket);
    return _z_send_ble_l2cap(self->_socket._ble_l2cap, ptr, len);
}

size_t _z_f_link_write_all_ble_l2cap(const _z_link_t *self, const uint8_t *ptr,
                                      size_t len) {
    return _z_send_ble_l2cap(self->_socket._ble_l2cap, ptr, len);
}

size_t _z_f_link_read_ble_l2cap(const _z_link_t *self, uint8_t *ptr,
                                 size_t len, _z_slice_t *addr) {
    size_t rb = _z_read_ble_l2cap(self->_socket._ble_l2cap, ptr, len);
    if ((rb > (size_t)0) && (rb != SIZE_MAX) && (addr != NULL)) {
        const char *remote = self->_socket._ble_l2cap._remote_addr;
        if (remote != NULL) {
            addr->len = strlen(remote);
            (void)memcpy((uint8_t *)addr->start, remote, addr->len);
        }
    }
    return rb;
}

size_t _z_f_link_read_socket_ble_l2cap(const _z_sys_net_socket_t socket,
                                        uint8_t *ptr, size_t len) {
    // BLE L2CAP does not support reading from arbitrary socket handles
    // in the same way as TCP. Use noop or platform-specific impl.
    _ZP_UNUSED(socket);
    _ZP_UNUSED(ptr);
    _ZP_UNUSED(len);
    return SIZE_MAX;
}

// ---------------------------------------------------------------------------
// Link Constructor
// ---------------------------------------------------------------------------

uint16_t _z_get_link_mtu_ble_l2cap(void) {
    return BLE_L2CAP_DEFAULT_MTU;
}

z_result_t _z_new_link_ble_l2cap(_z_link_t *zl, _z_endpoint_t endpoint) {
    z_result_t ret = _Z_RES_OK;

    zl->_type = _Z_LINK_TYPE_BLE_L2CAP;

    // L2CAP CoC is point-to-point, reliable, message-oriented (SOCK_SEQPACKET)
    zl->_cap._transport = Z_LINK_CAP_TRANSPORT_UNICAST;
    zl->_cap._flow = Z_LINK_CAP_FLOW_DATAGRAM;
    zl->_cap._is_reliable = true;

    zl->_mtu = _z_get_link_mtu_ble_l2cap();

    zl->_endpoint = endpoint;

    // Wire function pointers
    zl->_open_f = _z_f_link_open_ble_l2cap;
    zl->_listen_f = _z_f_link_listen_ble_l2cap;
    zl->_close_f = _z_f_link_close_ble_l2cap;
    zl->_free_f = _z_f_link_free_ble_l2cap;

    zl->_write_f = _z_f_link_write_ble_l2cap;
    zl->_write_all_f = _z_f_link_write_all_ble_l2cap;
    zl->_read_f = _z_f_link_read_ble_l2cap;
    zl->_read_exact_f = NULL;  // Not applicable: L2CAP CoC is datagram, not stream
    zl->_read_socket_f = _z_f_link_read_socket_ble_l2cap;

    return ret;
}

#endif /* Z_FEATURE_LINK_BLE_L2CAP == 1 */
