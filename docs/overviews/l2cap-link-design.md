# L2CAP CoC BLE Transport Link for zenoh-pico: Design Document

**Bead:** future-of-work-fcx
**Status:** Pre-work research and skeleton design (no hardware)
**Goal:** First BLE transport in the Zenoh ecosystem, using L2CAP CoC for direct socket-like BLE connections bypassing GATT entirely.

---

## Table of Contents

1. [zenoh-pico Link Abstraction Layer](#1-zenoh-pico-link-abstraction-layer)
2. [Interface Contract Checklist](#2-interface-contract-checklist)
3. [NimBLE L2CAP CoC API (ESP-IDF)](#3-nimble-l2cap-coc-api-esp-idf)
4. [Host-Side L2CAP CoC APIs](#4-host-side-l2cap-coc-apis)
5. [Link Design](#5-link-design)
6. [Skeleton Code Guide](#6-skeleton-code-guide)
7. [Upstream Contribution Path](#7-upstream-contribution-path)

---

## 1. zenoh-pico Link Abstraction Layer

### Architecture Overview

zenoh-pico link layer is a function-pointer-based polymorphism system. Each transport (TCP, UDP, BT SPP, Serial, WS, TLS, RawEth) implements the same set of function pointers stored in `_z_link_t`. The link lifecycle:

1. **Parse locator** -- `_z_endpoint_from_string()` parses `"proto/address#config"`
2. **Validate** -- `_z_endpoint_<proto>_valid()` checks schema + address
3. **Construct** -- `_z_new_link_<proto>()` fills the `_z_link_t` struct with function pointers, MTU, capabilities, and socket data
4. **Open/Listen** -- `zl->_open_f(zl)` or `zl->_listen_f(zl)` establishes the connection
5. **Read/Write** -- `zl->_read_f()` / `zl->_write_f()` for data transfer
6. **Close** -- `_z_link_clear()` calls `_close_f` then `_free_f`

### Key Source Files

| File | Purpose |
|------|---------|
| `include/zenoh-pico/link/link.h` | `_z_link_t` struct, function pointer typedefs, enums |
| `include/zenoh-pico/link/endpoint.h` | Locator/endpoint parsing, schema constants |
| `include/zenoh-pico/link/manager.h` | `_z_endpoint_<proto>_valid()` + `_z_new_link_<proto>()` decls |
| `include/zenoh-pico/link/config/bt.h` | BT config keys (mode, profile, timeout) |
| `include/zenoh-pico/link/transport/bt.h` | BT socket struct + low-level open/close/read/write |
| `include/zenoh-pico/link/transport/serial.h` | Serial low-level API |
| `include/zenoh-pico/link/transport/serial_protocol.h` | Serial socket struct, COBS framing, MTU |
| `src/link/link.c` | `_z_open_link()`, `_z_listen_link()`, dispatcher chain |
| `src/link/multicast/bt.c` | BT SPP link manager (validation, fn ptr wiring) |
| `src/link/unicast/serial.c` | Serial link manager |
| `src/link/unicast/tcp.c` | TCP link manager (canonical reference) |
| `src/link/transport/bt/bt_arduino_esp32.cpp` | BT SPP platform impl (Arduino/ESP32) |
| `src/link/transport/serial/uart_espidf.c` | Serial UART platform impl (ESP-IDF) |
| `include/zenoh-pico/config.h` | Feature flags |

### The _z_link_t Struct (Complete)

```c
enum _z_link_type_e {
    _Z_LINK_TYPE_TCP,
    _Z_LINK_TYPE_UDP,
    _Z_LINK_TYPE_BT,
    _Z_LINK_TYPE_SERIAL,
    _Z_LINK_TYPE_WS,
    _Z_LINK_TYPE_TLS,
    _Z_LINK_TYPE_RAWETH,
};

typedef enum {
    Z_LINK_CAP_TRANSPORT_UNICAST = 0,
    Z_LINK_CAP_TRANSPORT_MULTICAST = 1,
    Z_LINK_CAP_TRANSPORT_RAWETH = 2,
} _z_link_cap_transport_t;

typedef enum {
    Z_LINK_CAP_FLOW_DATAGRAM = 0,
    Z_LINK_CAP_FLOW_STREAM = 1,
} _z_link_cap_flow_t;

typedef struct _z_link_capabilities_t {
    uint8_t _transport : 2;
    uint8_t _flow : 1;
    uint8_t _is_reliable : 1;
    uint8_t _reserved : 4;
} _z_link_capabilities_t;

typedef struct _z_link_t {
    _z_endpoint_t _endpoint;
    int _type;
    union {
        _z_tcp_socket_t _tcp;
        _z_udp_socket_t _udp;
        _z_bt_socket_t _bt;
        _z_serial_socket_t _serial;
        _z_ws_socket_t _ws;
        _z_tls_socket_t _tls;
        _z_raweth_socket_t _raweth;
    } _socket;

    _z_f_link_open _open_f;
    _z_f_link_listen _listen_f;
    _z_f_link_close _close_f;
    _z_f_link_write _write_f;
    _z_f_link_write_all _write_all_f;
    _z_f_link_read _read_f;
    _z_f_link_read_exact _read_exact_f;
    _z_f_link_read_socket _read_socket_f;
    _z_f_link_free _free_f;

    uint16_t _mtu;
    _z_link_capabilities_t _cap;
} _z_link_t;
```

### Function Pointer Signatures

```c
typedef z_result_t (*_z_f_link_open)(struct _z_link_t *self);
typedef z_result_t (*_z_f_link_listen)(struct _z_link_t *self);
typedef void       (*_z_f_link_close)(struct _z_link_t *self);
typedef size_t     (*_z_f_link_write)(const struct _z_link_t *self,
                        const uint8_t *ptr, size_t len, _z_sys_net_socket_t *socket);
typedef size_t     (*_z_f_link_write_all)(const struct _z_link_t *self,
                        const uint8_t *ptr, size_t len);
typedef size_t     (*_z_f_link_read)(const struct _z_link_t *self,
                        uint8_t *ptr, size_t len, _z_slice_t *addr);
typedef size_t     (*_z_f_link_read_exact)(const struct _z_link_t *self,
                        uint8_t *ptr, size_t len, _z_slice_t *addr,
                        _z_sys_net_socket_t *socket);
typedef size_t     (*_z_f_link_read_socket)(const _z_sys_net_socket_t socket,
                        uint8_t *ptr, size_t len);
typedef void       (*_z_f_link_free)(struct _z_link_t *self);
```

### How Links Are Registered

In `src/link/link.c`, `_z_open_link()` uses a chain of if/else-if:

```c
if (_z_endpoint_tcp_valid(&ep) == _Z_RES_OK) {
    ret = _z_new_link_tcp(zl, &ep);
} else if (_z_endpoint_bt_valid(&ep) == _Z_RES_OK) {
    ret = _z_new_link_bt(zl, ep);
} else if (_z_endpoint_serial_valid(&ep) == _Z_RES_OK) {
    ret = _z_new_link_serial(zl, ep);
} ...
```

Each guarded by `#if Z_FEATURE_LINK_<X> == 1`. Adding a new link requires:

1. `Z_FEATURE_LINK_BLE_L2CAP` flag in `config.h`
2. `_Z_LINK_TYPE_BLE_L2CAP` enum value in `link.h`
3. `_z_ble_l2cap_socket_t` in the `_socket` union in `link.h`
4. `_z_endpoint_ble_l2cap_valid()` + `_z_new_link_ble_l2cap()` in `manager.h`
5. New entries in `_z_open_link()` and `_z_listen_link()` chains in `link.c`
6. New case in `_z_link_get_socket()` in `link.c`
7. `BLE_L2CAP_SCHEMA "ble"` in `endpoint.h`
8. Config key definitions in new `config/ble_l2cap.h`

### Existing BT SPP Link (Template Analysis)

The BT SPP link (`src/link/multicast/bt.c` + `bt_arduino_esp32.cpp`):

- Transport: `Z_LINK_CAP_TRANSPORT_MULTICAST`, Flow: `STREAM`, Reliable: `false`
- MTU: 128 bytes, Config: mode/profile/timeout
- Platform: Arduino ESP32 only (`BluetoothSerial` C++ API)
- Classic BT, not BLE. Byte-by-byte read with `delay(1)` polling

Key differences: BT SPP is Classic BT Arduino-only. We need BLE L2CAP CoC with ESP-IDF NimBLE (C), Linux bluez sockets (C), macOS CoreBluetooth (ObjC). L2CAP CoC is unicast, reliable.

### Serial Link (Template Analysis)

Serial (`src/link/unicast/serial.c` + `serial_protocol.h`):

- Transport: `UNICAST`, Flow: `DATAGRAM` (COBS framing), Reliable: `false`
- MTU: 1500, Uses COBS for packet boundaries
- Multiple platform impls: ESP-IDF, Arduino, POSIX tty, Zephyr

Key insight: Serial uses COBS because UART has no packet boundaries. **L2CAP CoC already provides SDU boundaries** -- COBS is NOT needed.

---

## 2. Interface Contract Checklist

### Files to Create

- [ ] `include/zenoh-pico/link/config/ble_l2cap.h`
- [ ] `include/zenoh-pico/link/transport/ble_l2cap.h`
- [ ] `src/link/unicast/ble_l2cap.c`
- [ ] `src/link/transport/ble_l2cap/ble_l2cap_espidf.c`
- [ ] `src/link/transport/ble_l2cap/ble_l2cap_linux.c`
- [ ] `src/link/transport/ble_l2cap/ble_l2cap_macos.m`

### Files to Modify

- [ ] `include/zenoh-pico/config.h` -- `Z_FEATURE_LINK_BLE_L2CAP 0`
- [ ] `include/zenoh-pico/link/link.h` -- enum + union member
- [ ] `include/zenoh-pico/link/endpoint.h` -- schema constant
- [ ] `include/zenoh-pico/link/manager.h` -- function declarations
- [ ] `src/link/link.c` -- open/listen/get_socket chains
- [ ] `CMakeLists.txt` -- feature flag + source conditionals

### Functions to Implement

| Function | Purpose |
|----------|---------|
| `_z_endpoint_ble_l2cap_valid` | Validate schema="ble", address present |
| `_z_new_link_ble_l2cap` | Fill link with fn ptrs, MTU, caps |
| `_z_f_link_open_ble_l2cap` | Connect to remote BLE device |
| `_z_f_link_listen_ble_l2cap` | Start L2CAP CoC server |
| `_z_f_link_close_ble_l2cap` | Disconnect channel |
| `_z_f_link_free_ble_l2cap` | Free resources |
| `_z_f_link_write_ble_l2cap` | Send data |
| `_z_f_link_write_all_ble_l2cap` | Send all data |
| `_z_f_link_read_ble_l2cap` | Receive data |
| `_z_f_link_read_exact_ble_l2cap` | Receive exact bytes |
| `_z_f_link_read_socket_ble_l2cap` | Read from raw socket |
| `_z_open_ble_l2cap` | Platform-specific connect |
| `_z_listen_ble_l2cap` | Platform-specific listen |
| `_z_close_ble_l2cap` | Platform-specific close |
| `_z_read_ble_l2cap` | Platform-specific read |
| `_z_send_ble_l2cap` | Platform-specific send |

---

## 3. NimBLE L2CAP CoC API (ESP-IDF)

### Key Functions

```c
int ble_l2cap_create_server(uint16_t psm, uint16_t mtu,
                            ble_l2cap_event_fn *cb, void *cb_arg);
int ble_l2cap_remove_server(uint16_t psm);
int ble_l2cap_connect(uint16_t conn_handle, uint16_t psm, uint16_t mtu,
                      struct os_mbuf *sdu_rx,
                      ble_l2cap_event_fn *cb, void *cb_arg);
int ble_l2cap_disconnect(struct ble_l2cap_chan *chan);
int ble_l2cap_send(struct ble_l2cap_chan *chan, struct os_mbuf *sdu_tx);
int ble_l2cap_recv_ready(struct ble_l2cap_chan *chan, struct os_mbuf *sdu_rx);
int ble_l2cap_get_chan_info(struct ble_l2cap_chan *chan,
                            struct ble_l2cap_chan_info *chan_info);
```

### Events

```c
#define BLE_L2CAP_EVENT_COC_CONNECTED           0
#define BLE_L2CAP_EVENT_COC_DISCONNECTED        1
#define BLE_L2CAP_EVENT_COC_ACCEPT              2
#define BLE_L2CAP_EVENT_COC_DATA_RECEIVED       3
#define BLE_L2CAP_EVENT_COC_TX_UNSTALLED        4
#define BLE_L2CAP_EVENT_COC_RECONFIG_COMPLETED  5
#define BLE_L2CAP_EVENT_COC_PEER_RECONFIGURED   6
```

### Event Structure

```c
struct ble_l2cap_event {
    uint8_t type;
    union {
        struct { int status; uint16_t conn_handle;
                 struct ble_l2cap_chan *chan; } connect;
        struct { uint16_t conn_handle;
                 struct ble_l2cap_chan *chan; } disconnect;
        struct { uint16_t conn_handle; uint16_t peer_sdu_size;
                 struct ble_l2cap_chan *chan; } accept;
        struct { uint16_t conn_handle; struct ble_l2cap_chan *chan;
                 struct os_mbuf *sdu_rx; } receive;
        struct { uint16_t conn_handle; struct ble_l2cap_chan *chan;
                 int status; } tx_unstalled;
        struct { int status; uint16_t conn_handle;
                 struct ble_l2cap_chan *chan; } reconfigured;
    };
};
```

### Channel Info

```c
struct ble_l2cap_chan_info {
    uint16_t scid, dcid;
    uint16_t our_l2cap_mtu, peer_l2cap_mtu;
    uint16_t psm;
    uint16_t our_coc_mtu, peer_coc_mtu;
};
```

### ESP32 Constraints

- Max SDU: 512 bytes (MTU=512, MPS=528)
- Memory pool: 20 buffers x 512 bytes pre-allocated
- MSYS_1 block size >= 536
- Credit flow: automatic. `ble_l2cap_send()` returns `BLE_HS_ESTALLED` when no credits

### sdkconfig

```
CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM=1
CONFIG_BT_NIMBLE_MSYS_1_BLOCK_SIZE=536
CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=20
```

### Thread Model: FreeRTOS Bridge

NimBLE is event-driven. zenoh-pico expects blocking read/write.

- **Read:** `xQueueReceive()` blocks. NimBLE `DATA_RECEIVED` callback does `xQueueSend()`.
- **Write:** `ble_l2cap_send()` directly (thread-safe). If `ESTALLED`, `xSemaphoreTake()` until `TX_UNSTALLED`.
- **Connect:** Block on semaphore until `COC_CONNECTED` fires.

---

## 4. Host-Side L2CAP CoC APIs

### 4.1 Linux (bluez)

Standard POSIX socket API. Kernel auto-selects LE Credit-Based Flow Control for LE addresses.

```c
int sock = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);

struct sockaddr_l2 {
    sa_family_t    l2_family;      // AF_BLUETOOTH
    unsigned short l2_psm;         // PSM
    bdaddr_t       l2_bdaddr;      // BT address
    unsigned short l2_cid;         // 0 for PSM-based
    uint8_t        l2_bdaddr_type; // BDADDR_LE_PUBLIC / BDADDR_LE_RANDOM
};
```

**Server:** bind() + setsockopt(BT_RCVMTU) + listen() + accept()

**Client:** setsockopt(BT_RCVMTU) + connect()

**I/O:** `recv()` / `send()` -- one SDU per call, blocking, standard POSIX.

**Gotchas:**
- `CAP_NET_RAW` or root required
- LE dynamic PSMs: 0x0080-0x00FF (odd only)
- `l2_bdaddr_type` MUST be LE variant for BLE
- `BT_RCVMTU` set before connect
- No scanning -- separate HCI/D-Bus for discovery
- Kernel >= 4.9

**Thread model:** Direct blocking. Maps perfectly to zenoh-pico.

### 4.2 macOS (CoreBluetooth)

`CBL2CAPChannel` (macOS 11+). Objective-C delegate API, no C sockets.

**Properties:** `inputStream`, `outputStream`, `PSM`, `peer`

**Peripheral:** `publishL2CAPChannel:` -> system assigns PSM via delegate

**Central:** `openL2CAPChannel:` -> delegate delivers channel

**Gotchas:**
- No C API -- need `.m` wrapper with `extern "C"`
- System-assigned PSM (peripheral side)
- Bluetooth entitlement may be required
- Run loop / dispatch queue needed
- NSStream is byte stream (no SDU boundaries at API level)

**Thread model:** dispatch_semaphore bridge needed.

### 4.3 Platform Matrix

| Feature | ESP32 (NimBLE) | Linux (bluez) | macOS (CoreBluetooth) |
|---------|---------------|---------------|----------------------|
| API | Callback (C) | POSIX socket | Delegate (ObjC) |
| Max SDU | 512 B | ~65535 B | System |
| PSM Control | App | App | System (periph) |
| Blocking I/O | No | Yes | No |
| Scanning | Separate | Separate | Separate |
| Permissions | None | CAP_NET_RAW | Entitlement |

---

## 5. Link Design

### 5.1 Locator Format

```
ble/<bd_addr>#psm=<psm>&mtu=<mtu>
```

Examples:
```
ble/AA:BB:CC:DD:EE:FF#psm=128          # Client
ble/AA:BB:CC:DD:EE:FF#psm=128&mtu=512  # With MTU
ble/*#psm=128                          # Server
```

Schema: `BLE_L2CAP_SCHEMA "ble"`

| Key | Name | Required | Default | Description |
|-----|------|----------|---------|-------------|
| 0x01 | psm | Yes | 128 | L2CAP PSM (0x80-0xFF for LE) |
| 0x02 | mtu | No | 512 | Max SDU size bytes |
| 0x03 | addr_type | No | public | public or random |
| 0x04 | tout | No | 10000 | Connect timeout ms |

### 5.2 Socket Structure

```c
typedef struct {
    _z_sys_net_socket_t _sock;
    char *_remote_addr;
    uint16_t _psm;
    uint16_t _mtu;
} _z_ble_l2cap_socket_t;
```

### 5.3 Capabilities

```c
zl->_type = _Z_LINK_TYPE_BLE_L2CAP;
zl->_cap._transport = Z_LINK_CAP_TRANSPORT_UNICAST;
zl->_cap._flow = Z_LINK_CAP_FLOW_STREAM;
zl->_cap._is_reliable = true;
```

Unicast (point-to-point), reliable (credit-based flow + link-layer retransmit), stream (zenoh transport handles framing with 2-byte length prefix, same as TCP).

### 5.4 Framing: No COBS

L2CAP CoC has SDU boundaries + reliable + ordered delivery. Treat as stream like TCP.

### 5.5 MTU Strategy

Both sides advertise preferred MTU. Effective = min(our, peer). ESP32 limit: 512. Store negotiated MTU in socket struct after connect.

### 5.6 Error Handling

| Error | Behavior |
|-------|----------|
| BLE disconnect | read() returns SIZE_MAX |
| Range loss | BLE supervision timeout -> disconnect |
| TX stall | Block until credits |
| GAP failure | open() returns error |

### 5.7 Throughput

BLE 4.2 + DLE: ~100-160 KB/s. Humanoid teleop budget: ~71 KB/s (heartbeat + joints + force/torque). Fits.

---

## 6. Skeleton Code Guide

See `l2cap-link-skeleton/` for stubbed implementations following zenoh-pico patterns.

| File | Description |
|------|-------------|
| `link_ble_l2cap.h` | Header with config keys, socket struct, all declarations |
| `link_ble_l2cap_linux.c` | Linux BlueZ L2CAP socket implementation |
| `link_ble_l2cap_manager.c` | Validation + function pointer wiring |

---

## 7. Upstream Contribution Path

### Requirements

1. **ECA:** http://www.eclipse.org/legal/ECA.php
2. Eclipse Foundation account
3. Signed-off-by in commits
4. License: EPL-2.0 OR Apache-2.0

### Relevant Issues

- **#810** ("Custom Transport"): open enhancement, discusses user-defined transports
- No BLE-specific issue exists -- file RFC first

### Strategy

1. File RFC issue on zenoh-pico
2. ESP32 first (smallest scope, highest impact)
3. Linux second (testing without hardware)
4. macOS third (ObjC complexity)

### Contact

- zenoh@zettascale.tech
- https://accounts.eclipse.org/mailing-list/zenoh-dev
