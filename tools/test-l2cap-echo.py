#!/usr/bin/env python3
"""
L2CAP CoC Echo Client for testing ESP32 L2CAP server.

Usage:
    python3 test-l2cap-echo.py <BLE_ADDRESS> [public|random]

Example:
    python3 test-l2cap-echo.py 28:05:A5:5A:42:4E

Requires:
    - Linux with BlueZ 5.x kernel support
    - CAP_NET_RAW capability or root
    - Kernel support for BLE L2CAP CoC (CONFIG_BT_LE)
"""

import ctypes
import ctypes.util
import os
import socket
import struct
import sys
import time

# L2CAP / Bluetooth constants (from linux/bluetooth.h)
AF_BLUETOOTH = 31
BTPROTO_L2CAP = 0
SOCK_SEQPACKET = 5
BDADDR_BREDR = 0
BDADDR_LE_PUBLIC = 1
BDADDR_LE_RANDOM = 2

# Must match ESP32 firmware
L2CAP_PSM = 0x0080
L2CAP_MTU = 512


def parse_bdaddr(addr_str):
    """Convert 'AA:BB:CC:DD:EE:FF' to 6-byte little-endian bdaddr_t."""
    parts = addr_str.split(':')
    if len(parts) != 6:
        raise ValueError(f"Invalid BLE address: {addr_str}")
    return bytes(int(p, 16) for p in reversed(parts))


def _make_sockaddr_l2(psm, addr_bytes, addr_type):
    """Build a sockaddr_l2 struct for BLE L2CAP CoC."""
    # struct sockaddr_l2 {
    #   sa_family_t l2_family;     // uint16  (2)
    #   uint16_t    l2_psm;        // uint16  (2) - little endian
    #   bdaddr_t    l2_bdaddr;     // uint8[6](6) - little endian
    #   uint16_t    l2_cid;        // uint16  (2)
    #   uint8_t     l2_bdaddr_type;// uint8   (1)
    # };  // total = 13, padded to 14 by compiler
    sockaddr = struct.pack('<HH6sHB', AF_BLUETOOTH, psm, addr_bytes, 0, addr_type)
    sockaddr += b'\x00'  # pad to 14 bytes
    return sockaddr


def bind_l2cap_le(sock, addr_type):
    """Bind local socket to BDADDR_ANY with LE address type.

    This is REQUIRED for BLE L2CAP CoC. Without binding, the kernel defaults
    to BR/EDR for the local side, causing a type mismatch. l2test always
    binds before connecting.
    """
    bdaddr_any = b'\x00' * 6
    sockaddr = _make_sockaddr_l2(0, bdaddr_any, addr_type)

    libc = ctypes.CDLL(ctypes.util.find_library('c'), use_errno=True)
    fd = sock.fileno()
    ret = libc.bind(fd, sockaddr, len(sockaddr))
    if ret != 0:
        errno = ctypes.get_errno()
        raise OSError(errno, os.strerror(errno))


def connect_l2cap_le(sock, addr_str, psm, addr_type):
    """Connect L2CAP socket with explicit LE address type via raw syscall.

    Python's socket.connect() for AF_BLUETOOTH only supports the (addr, psm)
    tuple format which defaults to BR/EDR. For BLE L2CAP CoC, we need to set
    bdaddr_type to BDADDR_LE_PUBLIC or BDADDR_LE_RANDOM in the sockaddr_l2.
    """
    addr_bytes = parse_bdaddr(addr_str)
    sockaddr = _make_sockaddr_l2(psm, addr_bytes, addr_type)

    # Use libc connect() directly to bypass Python's tuple format requirement
    libc = ctypes.CDLL(ctypes.util.find_library('c'), use_errno=True)
    fd = sock.fileno()
    ret = libc.connect(fd, sockaddr, len(sockaddr))
    if ret != 0:
        errno = ctypes.get_errno()
        raise OSError(errno, os.strerror(errno))


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <BLE_ADDRESS> [public|random]")
        print(f"Example: {sys.argv[0]} 28:05:A5:5A:42:4E")
        sys.exit(1)

    addr = sys.argv[1]
    addr_type = BDADDR_LE_PUBLIC
    if len(sys.argv) > 2 and sys.argv[2] == 'random':
        addr_type = BDADDR_LE_RANDOM

    print(f"Connecting to {addr} (PSM=0x{L2CAP_PSM:04X}, MTU={L2CAP_MTU}, "
          f"addr_type={'random' if addr_type == BDADDR_LE_RANDOM else 'public'})...")

    # Create L2CAP socket
    # SOCK_SEQPACKET preserves message boundaries (one SDU per send/recv)
    sock = socket.socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP)

    # Bind local socket to BDADDR_ANY with LE address type (required for BLE L2CAP CoC)
    try:
        bind_l2cap_le(sock, addr_type)
        print("Bound local socket with LE address type.")
    except OSError as e:
        print(f"Warning: bind failed ({e}), continuing anyway...")

    # Set receive MTU (SOL_BLUETOOTH=274, BT_RCVMTU=13)
    SOL_BLUETOOTH = 274
    BT_RCVMTU = 13
    try:
        sock.setsockopt(SOL_BLUETOOTH, BT_RCVMTU, struct.pack('<H', L2CAP_MTU))
        print(f"Set BT_RCVMTU={L2CAP_MTU}")
    except OSError as e:
        print(f"Warning: setsockopt BT_RCVMTU failed ({e})")

    try:
        connect_l2cap_le(sock, addr, L2CAP_PSM, addr_type)
    except OSError as e:
        print(f"Connection failed: {e}")
        print("Make sure:")
        print("  1. ESP32 is advertising (LED blinking)")
        print("  2. Running as root or have CAP_NET_RAW")
        print("  3. BLE device is not already connected elsewhere")
        print("  4. Try 'random' address type if 'public' fails")
        sys.exit(1)

    print(f"Connected! L2CAP CoC channel open.")

    # Wait for L2CAP CoC credit exchange to complete.
    # Without this delay, sends may fail with EAGAIN or data may be silently dropped
    # because the remote hasn't issued credits yet.
    print("Waiting 1s for L2CAP credit exchange...")
    time.sleep(1)
    print()

    # Test 1: Simple echo
    test_msgs = [
        b"hello-l2cap",
        b"zenoh-pico-heartbeat",
        b"A" * 100,    # 100 bytes
        b"B" * 480,    # near MTU
    ]

    passed = 0
    failed = 0

    for i, msg in enumerate(test_msgs):
        print(f"Test {i+1}: sending {len(msg)} bytes...", end=' ')
        try:
            # Retry on EAGAIN (credits not yet available)
            sock.setblocking(False)
            for attempt in range(10):
                try:
                    sock.send(msg)
                    break
                except BlockingIOError:
                    time.sleep(0.1)
            else:
                print("FAIL (EAGAIN after 10 retries)")
                failed += 1
                continue
            sock.setblocking(True)
            sock.settimeout(3.0)
            echo = sock.recv(L2CAP_MTU)

            if echo == msg:
                print(f"PASS (echo {len(echo)} bytes)")
                passed += 1
            else:
                print(f"FAIL (sent {len(msg)}, got {len(echo)} bytes)")
                if len(echo) < 50:
                    print(f"  sent: {msg}")
                    print(f"  got:  {echo}")
                failed += 1
        except socket.timeout:
            print("FAIL (timeout)")
            failed += 1
        except OSError as e:
            print(f"FAIL ({e})")
            failed += 1

    print()

    # Test 2: Echo throughput (if all echo tests passed)
    if failed == 0:
        print("Echo throughput (stop-and-wait): 100 x 480-byte SDUs...")
        payload = b"T" * 480
        n_sdus = 100
        t0 = time.monotonic()

        for _ in range(n_sdus):
            sock.send(payload)
            echo = sock.recv(L2CAP_MTU)

        elapsed = time.monotonic() - t0
        total_bytes = n_sdus * 480 * 2  # send + receive
        throughput = total_bytes / elapsed / 1024
        print(f"  {n_sdus} roundtrips in {elapsed:.2f}s")
        print(f"  Throughput: {throughput:.1f} KB/s (bidirectional)")
        print(f"  Latency: {elapsed/n_sdus*1000:.1f} ms/roundtrip")

    # Test 3: Unidirectional streaming (device → host) -- DISABLED in v1.
    # Bursting SDUs device->host exhausts Zephyr 2.7's TX-context pool under the
    # slow BlueZ central and wedges the peripheral. See nrf52-l2cap-echo/README.md
    # "Known limitations". Set to True only with a credit-paced firmware sender.
    if False:
        print()
        print("Streaming throughput (ESP32→Linux): 200 x 480-byte SDUs...")
        # Send STREAM command to trigger ESP32 burst
        sock.send(b"STREAM")
        sock.settimeout(30.0)

        n_expected = 200
        sdu_size = 480
        received = 0
        total_rx = 0
        t0 = time.monotonic()

        while received < n_expected:
            try:
                data = sock.recv(L2CAP_MTU)
                if not data:
                    break
                received += 1
                total_rx += len(data)
            except socket.timeout:
                print(f"  Timeout after {received} SDUs")
                break
            except OSError as e:
                print(f"  Error after {received} SDUs: {e}")
                break

        elapsed = time.monotonic() - t0
        if elapsed > 0 and received > 0:
            throughput = total_rx / elapsed / 1024
            print(f"  {received}/{n_expected} SDUs received ({total_rx} bytes)")
            print(f"  Time: {elapsed:.2f}s")
            print(f"  Throughput: {throughput:.1f} KB/s (unidirectional)")
        else:
            print(f"  No data received")

    # Test 4: Unidirectional streaming (Linux → ESP32)
    if failed == 0:
        print()
        print("Streaming throughput (Linux→ESP32): 200 x 480-byte SDUs...")
        # Switch ESP32 to sink mode — echoing back would exhaust the mbuf pool
        # and prevent L2CAP credit updates, stalling after ~46 SDUs.
        sock.setblocking(True)
        sock.settimeout(3.0)
        sock.send(b"SINK\x00\x00")
        time.sleep(0.5)  # let ESP32 process the command
        payload = bytearray(480)
        n_sdus = 200
        sent = 0

        # Use blocking mode with long timeout. Pace sends with a small delay
        # to let the ESP32 process received data and issue L2CAP credit updates.
        sock.setblocking(True)
        sock.settimeout(10.0)
        t0 = time.monotonic()

        for i in range(n_sdus):
            payload[0] = i & 0xFF
            payload[1] = (i >> 8) & 0xFF
            try:
                sock.send(bytes(payload))
                sent += 1
            except OSError as e:
                print(f"  Send failed at SDU {i}: {e}")
                break

        elapsed = time.monotonic() - t0
        total_tx = sent * 480
        if elapsed > 0 and sent > 0:
            throughput = total_tx / elapsed / 1024
            print(f"  {sent}/{n_sdus} SDUs sent ({total_tx} bytes)")
            print(f"  Time: {elapsed:.2f}s")
            print(f"  Throughput: {throughput:.1f} KB/s (unidirectional)")
        time.sleep(1)  # let ESP32 process

        # Restore echo mode
        try:
            sock.send(b"ECHO\x00\x00")
        except OSError:
            pass

    print()
    print(f"Results: {passed} passed, {failed} failed")

    sock.close()
    print("Done.")


if __name__ == '__main__':
    main()
