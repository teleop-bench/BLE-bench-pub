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

// This file would live at:
//   src/link/transport/ble_l2cap/ble_l2cap_linux.c
//
// Linux bluez implementation of BLE L2CAP CoC transport link.
// Uses standard POSIX socket API with AF_BLUETOOTH / BTPROTO_L2CAP.
// This is the simplest implementation: bluez L2CAP sockets are blocking
// and map directly to zenoh-pico's read/write model.
//
// IMPORTANT: Requires CAP_NET_RAW capability or root privileges.
// IMPORTANT: BLE scanning/advertising is NOT handled here. The device
// must already be paired/connected at the GAP level, or use a separate
// tool (bluetoothctl, hcitool) for discovery.

#include "zenoh-pico/config.h"

#if Z_FEATURE_LINK_BLE_L2CAP == 1 && defined(ZENOH_LINUX)

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

// BlueZ headers
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>

// Socket options for LE MTU (added in kernel 4.9)
#ifndef BT_RCVMTU
#define BT_RCVMTU 13
#endif
#ifndef BT_SNDMTU
#define BT_SNDMTU 14
#endif

// BLE address types
#ifndef BDADDR_LE_PUBLIC
#define BDADDR_LE_PUBLIC 0x01
#endif
#ifndef BDADDR_LE_RANDOM
#define BDADDR_LE_RANDOM 0x02
#endif

#include "zenoh-pico/link/transport/ble_l2cap.h"  // link_ble_l2cap.h
#include "zenoh-pico/utils/logging.h"
#include "zenoh-pico/utils/pointers.h"

// ---------------------------------------------------------------------------
// Helper: Set socket timeout
// ---------------------------------------------------------------------------

static void _z_ble_set_timeout(int fd, uint32_t tout_ms) {
    if (tout_ms == 0) {
        return;  // No timeout = blocking forever
    }

    struct timeval tv;
    tv.tv_sec = tout_ms / 1000;
    tv.tv_usec = (tout_ms % 1000) * 1000;

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

z_result_t _z_open_ble_l2cap(_z_ble_l2cap_socket_t *sock, const char *addr,
                              uint8_t addr_type, uint16_t psm, uint16_t mtu,
                              uint32_t tout_ms) {
    if (sock == NULL || addr == NULL) {
        return _Z_ERR_INVALID;
    }

    // Step 1: Create L2CAP socket
    // SOCK_SEQPACKET gives us reliable, sequenced, message-boundary-preserving
    // delivery — perfect for L2CAP CoC.
    int fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (fd < 0) {
        _Z_ERROR("BLE L2CAP socket() failed: %s", strerror(errno));
        return _Z_ERR_TRANSPORT_OPEN_FAILED;
    }

    // Step 2: Set receive MTU (MUST be done before connect)
    if (setsockopt(fd, SOL_BLUETOOTH, BT_RCVMTU, &mtu, sizeof(mtu)) < 0) {
        _Z_ERROR("BLE L2CAP setsockopt(BT_RCVMTU) failed: %s", strerror(errno));
        // Non-fatal: kernel will use default MTU
    }

    // Step 3: Set timeout
    _z_ble_set_timeout(fd, tout_ms);

    // Step 4: Bind local address (optional for client, but good practice)
    struct sockaddr_l2 local_addr = {0};
    local_addr.l2_family = AF_BLUETOOTH;
    local_addr.l2_bdaddr_type = BDADDR_LE_PUBLIC;
    bacpy(&local_addr.l2_bdaddr, BDADDR_ANY);
    // l2_psm = 0 for client (kernel assigns)
    // l2_cid = 0 for PSM-based connection

    if (bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        _Z_ERROR("BLE L2CAP bind() failed: %s", strerror(errno));
        // Non-fatal for client
    }

    // Step 5: Set up remote address and connect
    struct sockaddr_l2 remote_addr = {0};
    remote_addr.l2_family = AF_BLUETOOTH;
    remote_addr.l2_psm = htobs(psm);
    remote_addr.l2_cid = 0;  // PSM-based
    remote_addr.l2_bdaddr_type = (addr_type == _Z_BLE_ADDR_TYPE_RANDOM)
                                     ? BDADDR_LE_RANDOM
                                     : BDADDR_LE_PUBLIC;

    // Parse address string "AA:BB:CC:DD:EE:FF" to bdaddr_t
    if (str2ba(addr, &remote_addr.l2_bdaddr) != 0) {
        _Z_ERROR("BLE L2CAP: invalid address '%s'", addr);
        close(fd);
        return _Z_ERR_CONFIG_LOCATOR_INVALID;
    }

    // Step 6: Connect (blocking)
    // NOTE: The BLE device must already be known to the kernel (paired or
    // recently scanned). The L2CAP socket API does NOT perform BLE scanning.
    // Use `bluetoothctl` or hci_le_create_connection() separately.
    if (connect(fd, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
        _Z_ERROR("BLE L2CAP connect() failed: %s", strerror(errno));
        close(fd);
        return _Z_ERR_TRANSPORT_OPEN_FAILED;
    }

    // Step 7: Query negotiated send MTU
    uint16_t snd_mtu = 0;
    socklen_t optlen = sizeof(snd_mtu);
    if (getsockopt(fd, SOL_BLUETOOTH, BT_SNDMTU, &snd_mtu, &optlen) == 0) {
        sock->_mtu = (snd_mtu < mtu) ? snd_mtu : mtu;
    } else {
        sock->_mtu = mtu;
    }

    // Fill socket structure
    sock->_sock._fd = fd;
    sock->_psm = psm;
    // _remote_addr is set by caller (link manager) before this call

    _Z_INFO("BLE L2CAP open: addr=%s psm=%u mtu=%u", addr, psm, sock->_mtu);

    return _Z_RES_OK;
}

z_result_t _z_listen_ble_l2cap(_z_ble_l2cap_socket_t *sock, uint16_t psm,
                                uint16_t mtu, uint32_t tout_ms) {
    if (sock == NULL) {
        return _Z_ERR_INVALID;
    }

    // Step 1: Create listening socket
    int listen_fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (listen_fd < 0) {
        _Z_ERROR("BLE L2CAP socket() failed: %s", strerror(errno));
        return _Z_ERR_TRANSPORT_OPEN_FAILED;
    }

    // Step 2: Set receive MTU (before bind/listen)
    if (setsockopt(listen_fd, SOL_BLUETOOTH, BT_RCVMTU, &mtu, sizeof(mtu)) < 0) {
        _Z_ERROR("BLE L2CAP setsockopt(BT_RCVMTU) failed: %s", strerror(errno));
    }

    // Step 3: Set accept timeout
    _z_ble_set_timeout(listen_fd, tout_ms);

    // Step 4: Bind to local address with PSM
    struct sockaddr_l2 local_addr = {0};
    local_addr.l2_family = AF_BLUETOOTH;
    local_addr.l2_psm = htobs(psm);
    local_addr.l2_cid = 0;
    bacpy(&local_addr.l2_bdaddr, BDADDR_ANY);
    local_addr.l2_bdaddr_type = BDADDR_LE_PUBLIC;

    if (bind(listen_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        _Z_ERROR("BLE L2CAP bind() failed: %s", strerror(errno));
        close(listen_fd);
        return _Z_ERR_TRANSPORT_OPEN_FAILED;
    }

    // Step 5: Listen
    if (listen(listen_fd, 1) < 0) {
        _Z_ERROR("BLE L2CAP listen() failed: %s", strerror(errno));
        close(listen_fd);
        return _Z_ERR_TRANSPORT_OPEN_FAILED;
    }

    // NOTE: BLE advertising must be started separately for clients to
    // discover this device. Use `bluetoothctl` or HCI commands:
    //   bluetoothctl advertise on

    _Z_INFO("BLE L2CAP listening: psm=%u mtu=%u (start advertising separately)",
            psm, mtu);

    // Step 6: Accept (blocking, with timeout from SO_RCVTIMEO)
    struct sockaddr_l2 peer_addr = {0};
    socklen_t peer_len = sizeof(peer_addr);
    int client_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &peer_len);
    if (client_fd < 0) {
        _Z_ERROR("BLE L2CAP accept() failed: %s", strerror(errno));
        close(listen_fd);
        return _Z_ERR_TRANSPORT_OPEN_FAILED;
    }

    // Close listening socket (single connection mode)
    close(listen_fd);

    // Step 7: Query negotiated send MTU
    uint16_t snd_mtu = 0;
    socklen_t optlen = sizeof(snd_mtu);
    if (getsockopt(client_fd, SOL_BLUETOOTH, BT_SNDMTU, &snd_mtu, &optlen) == 0) {
        sock->_mtu = (snd_mtu < mtu) ? snd_mtu : mtu;
    } else {
        sock->_mtu = mtu;
    }

    sock->_sock._fd = client_fd;
    sock->_psm = psm;

    // Convert peer address to string
    char addr_str[18];
    ba2str(&peer_addr.l2_bdaddr, addr_str);
    sock->_remote_addr = strdup(addr_str);

    _Z_INFO("BLE L2CAP accepted: peer=%s psm=%u mtu=%u",
            addr_str, psm, sock->_mtu);

    return _Z_RES_OK;
}

void _z_close_ble_l2cap(_z_ble_l2cap_socket_t *sock) {
    if (sock != NULL && sock->_sock._fd >= 0) {
        close(sock->_sock._fd);
        sock->_sock._fd = -1;
    }
    if (sock != NULL && sock->_remote_addr != NULL) {
        free(sock->_remote_addr);
        sock->_remote_addr = NULL;
    }
}

size_t _z_read_ble_l2cap(const _z_ble_l2cap_socket_t sock, uint8_t *ptr,
                          size_t len) {
    // recv() on SOCK_SEQPACKET returns one complete SDU per call.
    // Blocking by default (with optional SO_RCVTIMEO).
    ssize_t rb = recv(sock._sock._fd, ptr, len, 0);
    if (rb <= 0) {
        // 0 = orderly shutdown, <0 = error
        return SIZE_MAX;
    }
    return (size_t)rb;
}

size_t _z_send_ble_l2cap(const _z_ble_l2cap_socket_t sock, const uint8_t *ptr,
                          size_t len) {
    // send() on SOCK_SEQPACKET sends one complete SDU.
    // Blocking by default. Kernel handles credit-based flow control.
    ssize_t wb = send(sock._sock._fd, ptr, len, 0);
    if (wb < 0) {
        _Z_ERROR("BLE L2CAP send() failed: %s", strerror(errno));
        return SIZE_MAX;
    }
    return (size_t)wb;
}

#endif /* Z_FEATURE_LINK_BLE_L2CAP == 1 && defined(ZENOH_LINUX) */
