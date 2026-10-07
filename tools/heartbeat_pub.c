// BLE Heartbeat Fallback PoC - zenoh-pico serial heartbeat publisher
//
// Publishes a heartbeat message (timestamp + sequence number) at 10 Hz
// over zenoh-pico serial transport link.
//
// Usage:
//   ./heartbeat_pub -e "serial//dev/ttyUSB0#baudrate=115200"
//
// Serial locator format: serial/<device>#baudrate=<rate>
// For device paths with leading slash: serial//dev/ttyUSB0#baudrate=115200

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zenoh-pico.h>

#if Z_FEATURE_PUBLICATION == 1 && Z_FEATURE_LINK_SERIAL == 1

static volatile int running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

int main(int argc, char **argv) {
    const char *keyexpr = "heartbeat/ble";
    const char *serial_endpoint = "serial//dev/ttyUSB0#baudrate=115200";
    const char *mode = "client";
    int opt;

    while ((opt = getopt(argc, argv, "k:e:m:h")) != -1) {
        switch (opt) {
            case 'k':
                keyexpr = optarg;
                break;
            case 'e':
                serial_endpoint = optarg;
                break;
            case 'm':
                mode = optarg;
                break;
            case 'h':
                printf("Usage: %s [options]\n", argv[0]);
                printf("  -k <keyexpr>     Key expression (default: heartbeat/ble)\n");
                printf("  -e <endpoint>    Serial endpoint (default: serial//dev/ttyUSB0#baudrate=115200)\n");
                printf("  -m <mode>        Zenoh mode: client|peer (default: client)\n");
                return 0;
            default:
                return 1;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("=== BLE Heartbeat Publisher ===\n");
    printf("Key expression: %s\n", keyexpr);
    printf("Serial endpoint: %s\n", serial_endpoint);
    printf("Mode: %s\n", mode);

    // Configure zenoh-pico session
    z_owned_config_t config;
    z_config_default(&config);
    zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, mode);
    zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, serial_endpoint);

    printf("Opening session over serial...\n");
    z_owned_session_t session;
    if (z_open(&session, z_move(config), NULL) < 0) {
        printf("ERROR: Unable to open zenoh session over serial!\n");
        printf("  Check that the serial device exists and baudrate matches.\n");
        return -1;
    }
    printf("Session opened successfully.\n");

    // Declare publisher
    z_owned_publisher_t pub;
    z_view_keyexpr_t ke;
    if (z_view_keyexpr_from_str(&ke, keyexpr) < 0) {
        printf("ERROR: Invalid key expression: %s\n", keyexpr);
        z_drop(z_move(session));
        return -1;
    }
    if (z_declare_publisher(z_loan(session), &pub, z_loan(ke), NULL) < 0) {
        printf("ERROR: Unable to declare publisher!\n");
        z_drop(z_move(session));
        return -1;
    }
    printf("Publisher declared for '%s'\n", keyexpr);

    // Publish heartbeat at 10 Hz
    printf("Publishing heartbeat at 10 Hz. Press CTRL-C to quit.\n");
    uint32_t seq = 0;
    char buf[64];
    while (running) {
        uint64_t ts = now_ms();
        int len = snprintf(buf, sizeof(buf), "HB:%u:%lu", seq, (unsigned long)ts);

        z_owned_bytes_t payload;
        z_bytes_copy_from_buf(&payload, (const uint8_t *)buf, (size_t)len);

        z_publisher_put(z_loan(pub), z_move(payload), NULL);
        printf("[%u] heartbeat ts=%lu\n", seq, (unsigned long)ts);

        seq++;
        z_sleep_ms(100);  // 10 Hz
    }

    printf("\nShutting down...\n");
    z_drop(z_move(pub));
    z_drop(z_move(session));
    printf("Done.\n");
    return 0;
}

#else
int main(void) {
    printf("ERROR: zenoh-pico must be compiled with Z_FEATURE_PUBLICATION=1 and Z_FEATURE_LINK_SERIAL=1\n");
    printf("  cmake -DZ_FEATURE_LINK_SERIAL=1 ...\n");
    return -2;
}
#endif
