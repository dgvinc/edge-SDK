/* SPDX-License-Identifier: MIT
 *
 * EDGE Glasses C API — test suite.
 *
 * Deliberately compiled as C (not C++): it proves edge_glasses.h is usable from
 * a pure-C translation unit, which is how NeuroGuide's C code would consume it.
 */

#include <stdio.h>
#include <string.h>

#include "edge/edge_glasses.h"

static int g_checks = 0;
static int g_failures = 0;
static const char* g_current = "";

static void section(const char* name) {
    g_current = name;
    printf("  %s\n", name);
}

static void check(int cond, const char* what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        printf("    FAIL [%s] %s\n", g_current, what);
    }
}

static void check_frame(int n, const uint8_t* got, const uint8_t* want, size_t want_len,
                        const char* what) {
    size_t i;
    ++g_checks;
    if (n != (int)want_len || memcmp(got, want, want_len) != 0) {
        ++g_failures;
        printf("    FAIL [%s] %s\n", g_current, what);
        printf("         expected");
        for (i = 0; i < want_len; ++i) printf(" %02X", want[i]);
        printf("\n         actual  ");
        if (n < 0) {
            printf(" (error %d: %s)", n, edge_last_error());
        } else {
            for (i = 0; i < (size_t)n; ++i) printf(" %02X", got[i]);
        }
        printf("\n");
    }
}

/* ------------------------------------------------------------------------ */
/* A fake BLE stack, so the controller tier is exercised without hardware.    */
/* ------------------------------------------------------------------------ */

#define MAX_WRITES 64

typedef struct {
    uint8_t bytes[MAX_WRITES][EDGE_MAX_FRAME];
    size_t lengths[MAX_WRITES];
    int with_response[MAX_WRITES];
    int count;
    int connected;
    int fail_next;
} fake_ble;

static int fake_write(void* user, const uint8_t* data, size_t len, int with_response) {
    fake_ble* ble = (fake_ble*)user;
    if (ble->fail_next > 0) {
        --ble->fail_next;
        return -1;
    }
    if (ble->count < MAX_WRITES && len <= EDGE_MAX_FRAME) {
        memcpy(ble->bytes[ble->count], data, len);
        ble->lengths[ble->count] = len;
        ble->with_response[ble->count] = with_response;
        ++ble->count;
    }
    return 0;
}

static int fake_connected(void* user) { return ((fake_ble*)user)->connected; }

static int fake_battery(void* user, int* out_level) {
    (void)user;
    *out_level = 63;
    return 1;
}

/* ------------------------------------------------------------------------ */

static void test_frame_builders(void) {
    uint8_t buf[EDGE_MAX_FRAME];
    int n;

    printf("\nC API: frame builders\n");

    section("legacy opacity is a single byte");
    {
        uint8_t want[1];
        want[0] = 0x80;
        n = edge_frame_opacity(buf, sizeof buf, 128);
        check_frame(n, buf, want, 1, "opacity(128)");
        check(n == 1, "opacity is exactly 1 byte, never padded");
    }

    section("static duty and clamping");
    {
        uint8_t want[2];
        want[0] = 0xA5;
        want[1] = 50;
        n = edge_frame_set_static(buf, sizeof buf, 50);
        check_frame(n, buf, want, 2, "set_static(50)");
        want[1] = 100;
        n = edge_frame_set_static(buf, sizeof buf, 255);
        check_frame(n, buf, want, 2, "set_static clamps to 100");
        want[1] = 0;
        n = edge_frame_set_static(buf, sizeof buf, -5);
        check_frame(n, buf, want, 2, "set_static clamps to 0");
    }

    section("parameter frames");
    {
        uint8_t want[2];
        want[0] = 0xA2;
        want[1] = 80;
        check_frame(edge_frame_set_brightness(buf, sizeof buf, 80), buf, want, 2, "brightness 80");
        want[0] = 0xA4;
        want[1] = 60;
        check_frame(edge_frame_set_duration(buf, sizeof buf, 60), buf, want, 2, "duration 60");
        want[0] = 0xA4;
        want[1] = 60;
        check_frame(edge_frame_set_duration(buf, sizeof buf, 999), buf, want, 2, "duration clamps");
        want[0] = 0xAB;
        want[1] = 10;
        check_frame(edge_frame_set_strobe_frequency(buf, sizeof buf, 10), buf, want, 2, "10 Hz");
        want[0] = 0xAC;
        want[1] = 50;
        check_frame(edge_frame_set_strobe_duty(buf, sizeof buf, 50), buf, want, 2, "strobe duty");
        want[0] = 0xA0;
        want[1] = 8;
        check_frame(edge_frame_set_lens_smoothing(buf, sizeof buf, 80), buf, want, 2,
                    "smoothing 80 ms -> tau 8");
        want[0] = 0xA1;
        want[1] = 5;
        check_frame(edge_frame_set_lens_max_rate(buf, sizeof buf, 5), buf, want, 2, "max rate 5");
        want[0] = 0xA3;
        want[1] = 1;
        check_frame(edge_frame_set_disconnect_behavior(buf, sizeof buf, 1), buf, want, 2,
                    "fail clear");
    }

    section("mode frames are padded to 2 bytes");
    {
        uint8_t want[2];
        want[0] = 0xA6;
        want[1] = 0x00;
        check_frame(edge_frame_start_strobe(buf, sizeof buf), buf, want, 2, "start strobe");
        want[0] = 0xB0;
        want[1] = 0x00;
        check_frame(edge_frame_start_breathe(buf, sizeof buf, 0), buf, want, 2, "breathe");
        want[1] = 0x01;
        check_frame(edge_frame_start_breathe(buf, sizeof buf, 1), buf, want, 2, "breathe+strobe");
        want[0] = 0xA7;
        want[1] = 0x00;
        check_frame(edge_frame_sleep(buf, sizeof buf), buf, want, 2, "sleep");
        want[0] = 0xBF;
        want[1] = 0x00;
        check_frame(edge_frame_factory_reset(buf, sizeof buf), buf, want, 2, "factory reset");
        want[0] = 0xB5;
        want[1] = 0x01;
        check_frame(edge_frame_set_breathe_waveform(buf, sizeof buf, EDGE_WAVEFORM_LINEAR), buf,
                    want, 2, "linear waveform");
    }

    section("sync_breath packs cycle_ms as u16 little-endian");
    {
        uint8_t want[4];
        want[0] = 0xBA;
        want[1] = 0x7C;
        want[2] = 0x15;
        want[3] = 40;
        check_frame(edge_frame_sync_breath(buf, sizeof buf, 5500, 40), buf, want, 4, "5500 ms");
        want[1] = 0xFF;
        want[2] = 0xFF;
        check_frame(edge_frame_sync_breath(buf, sizeof buf, 70000, 40), buf, want, 4,
                    "clamps to u16");
    }

    section("command() enforces the >= 2-byte rule");
    {
        uint8_t want[4];
        uint8_t payload[3];
        want[0] = 0xA6;
        want[1] = 0x00;
        check_frame(edge_frame_command(buf, sizeof buf, 0xA6, NULL, 0), buf, want, 2,
                    "no payload is padded");
        payload[0] = 80;
        want[0] = 0xA2;
        want[1] = 80;
        check_frame(edge_frame_command(buf, sizeof buf, 0xA2, payload, 1), buf, want, 2,
                    "1-byte payload");
        /* The 3-byte deci-Hz strobe form: 13.5 Hz -> 135 -> 0x0087 LE */
        payload[0] = 0xAB;
        want[0] = 0xAB;
        want[1] = 0x87;
        want[2] = 0x00;
        {
            uint8_t dhz[2];
            dhz[0] = 135 & 0xFF;
            dhz[1] = (135 >> 8) & 0xFF;
            check_frame(edge_frame_command(buf, sizeof buf, 0xAB, dhz, 2), buf, want, 3,
                        "deci-Hz strobe 13.5 Hz");
        }
    }

    section("buffer errors are reported, not overflowed");
    {
        uint8_t small[1];
        n = edge_frame_set_static(small, sizeof small, 50);
        check(n == EDGE_ERR_INVALID_ARG, "too-small buffer returns EDGE_ERR_INVALID_ARG");
        check(strlen(edge_last_error()) > 0, "a message is available");
        n = edge_frame_set_static(NULL, 8, 50);
        check(n == EDGE_ERR_INVALID_ARG, "NULL buffer returns EDGE_ERR_INVALID_ARG");
    }
}

static void test_controller(void) {
    fake_ble ble;
    edge_glasses g = NULL;
    edge_status st;

    printf("\nC API: controller\n");

    memset(&ble, 0, sizeof ble);
    ble.connected = 1;

    section("create over a caller-supplied BLE stack");
    st = edge_glasses_create_with_callbacks(fake_write, fake_connected, &ble, &g);
    check(st == EDGE_OK, "created");
    check(g != NULL, "handle is non-NULL");
    check(edge_glasses_is_connected(g) == 1, "reports connected");

    section("NULL arguments are rejected");
    {
        edge_glasses bad = NULL;
        check(edge_glasses_create_with_callbacks(NULL, fake_connected, &ble, &bad) ==
                  EDGE_ERR_INVALID_ARG,
              "NULL write callback rejected");
        check(edge_glasses_create_with_callbacks(fake_write, NULL, &ble, &bad) ==
                  EDGE_ERR_INVALID_ARG,
              "NULL connected callback rejected");
        check(edge_glasses_create_with_callbacks(fake_write, fake_connected, &ble, NULL) ==
                  EDGE_ERR_INVALID_ARG,
              "NULL out_handle rejected");
    }

    section("commands reach the transport with the right bytes");
    ble.count = 0;
    check(edge_glasses_set_static(g, 50) == EDGE_OK, "set_static ok");
    check(ble.count == 1, "one write");
    check(ble.lengths[0] == 2 && ble.bytes[0][0] == 0xA5 && ble.bytes[0][1] == 50, "[0xA5, 50]");
    check(ble.with_response[0] == 1, "with-response");

    ble.count = 0;
    check(edge_glasses_set_opacity(g, 200) == EDGE_OK, "set_opacity ok");
    check(ble.count == 1 && ble.lengths[0] == 1 && ble.bytes[0][0] == 200,
          "opacity stays a single byte");

    ble.count = 0;
    check(edge_glasses_clear(g) == EDGE_OK, "clear ok");
    check(ble.lengths[0] == 1 && ble.bytes[0][0] == 0x00, "clear = 1-byte 0x00");

    section("optional parameters: negative means \"leave persisted\"");
    ble.count = 0;
    check(edge_glasses_start_strobe(g, -1, -1) == EDGE_OK, "start_strobe bare");
    check(ble.count == 1, "only the mode write");
    ble.count = 0;
    check(edge_glasses_start_strobe(g, 10, 50) == EDGE_OK, "start_strobe full");
    check(ble.count == 3, "frequency + duty + mode");

    ble.count = 0;
    check(edge_glasses_start_breathe(g, -1, -1, -1, -1, -1, 0) == EDGE_OK, "breathe bare");
    check(ble.count == 1 && ble.bytes[0][0] == 0xB0 && ble.bytes[0][1] == 0x00, "just [0xB0, 0]");

    ble.count = 0;
    check(edge_glasses_start_breathe(g, 5, 40, 1000, 500, EDGE_WAVEFORM_SINE, 0) == EDGE_OK,
          "breathe full");
    check(ble.count == 6, "5 parameters + mode");
    check(ble.bytes[0][0] == 0xB1 && ble.bytes[0][1] == 5, "bpm first");
    check(ble.bytes[5][0] == 0xB0 && ble.bytes[5][1] == 0x00, "mode last");

    section("preset sessions");
    ble.count = 0;
    check(edge_glasses_session_focus(g, 15) == EDGE_OK, "session_focus ok");
    check(ble.count == 4, "4 writes");
    check(ble.bytes[0][0] == 0xAB && ble.bytes[0][1] == 12, "12 Hz strobe");
    check(ble.bytes[2][0] == 0xB0 && ble.bytes[2][1] == 0x01, "breathe+strobe");
    check(ble.bytes[3][0] == 0xA4 && ble.bytes[3][1] == 15, "duration last");

    section("fast-write is opt-in and changes the response flag");
    check(edge_glasses_supports_fast_write(g) == 0, "off by default");
    check(edge_glasses_set_fast_write(g, 1) == EDGE_OK, "declared");
    check(edge_glasses_supports_fast_write(g) == 1, "reported on");

    section("battery");
    {
        int level = -1;
        check(edge_glasses_get_battery(g, &level) == EDGE_ERR_UNSUPPORTED,
              "no reader -> EDGE_ERR_UNSUPPORTED");
        check(edge_glasses_set_battery_callback(g, fake_battery) == EDGE_OK, "reader installed");
        check(edge_glasses_get_battery(g, &level) == EDGE_OK, "read ok");
        check(level == 63, "level 63");
        check(edge_glasses_get_battery(g, NULL) == EDGE_ERR_INVALID_ARG, "NULL out rejected");
    }

    section("errors map to status codes, never exceptions");
    ble.fail_next = 1;
    check(edge_glasses_set_static(g, 10) == EDGE_ERR_COMMAND, "write failure -> EDGE_ERR_COMMAND");
    check(strlen(edge_last_error()) > 0, "message recorded");
    ble.connected = 0;
    check(edge_glasses_set_static(g, 10) == EDGE_ERR_NOT_CONNECTED,
          "disconnected -> EDGE_ERR_NOT_CONNECTED");
    check(edge_glasses_is_connected(g) == 0, "is_connected reports 0");
    ble.connected = 1;

    section("NULL handle is tolerated everywhere");
    check(edge_glasses_set_static(NULL, 10) == EDGE_ERR_INVALID_ARG, "set_static(NULL)");
    check(edge_glasses_is_connected(NULL) == 0, "is_connected(NULL)");
    check(edge_glasses_supports_fast_write(NULL) == 0, "supports_fast_write(NULL)");
    edge_glasses_destroy(NULL); /* must not crash */

    section("status names and version are available");
    check(strcmp(edge_status_name(EDGE_OK), "EDGE_OK") == 0, "EDGE_OK");
    check(strcmp(edge_status_name(EDGE_ERR_NOT_CONNECTED), "EDGE_ERR_NOT_CONNECTED") == 0,
          "EDGE_ERR_NOT_CONNECTED");
    check(strlen(edge_version()) > 0, "version string");

    edge_glasses_destroy(g);
}

static void test_stream(void) {
    fake_ble ble;
    edge_glasses g = NULL;
    edge_stream s = NULL;

    printf("\nC API: feedback stream\n");

    memset(&ble, 0, sizeof ble);
    ble.connected = 1;
    edge_glasses_create_with_callbacks(fake_write, fake_connected, &ble, &g);

    section("open, feed, and stop");
    check(edge_glasses_start_feedback_stream(g, 45.0, &s) == EDGE_OK, "stream opened");
    check(s != NULL, "stream handle is non-NULL");
    check(edge_stream_feed(s, 60) == EDGE_OK, "feed accepted");
    check(edge_stream_feed_reward(s, 0.5) == EDGE_OK, "feed_reward accepted");

    section("reward_event writes immediately");
    ble.count = 0;
    check(edge_stream_reward_event(s, 0, 0) == EDGE_OK, "reward delivered");
    check(ble.count >= 1, "a write happened");
    check(ble.bytes[0][0] == 0xA5, "0xA5 static frame");

    section("stop clears the lens and invalidates the handle");
    ble.count = 0;
    check(edge_stream_stop(s, 1) == EDGE_OK, "stopped");
    check(ble.count == 1 && ble.lengths[0] == 1 && ble.bytes[0][0] == 0x00, "clear write");
    check(edge_stream_stop(s, 1) == EDGE_ERR_INVALID_ARG, "second stop is rejected");
    check(edge_stream_feed(s, 10) == EDGE_ERR_INVALID_ARG, "feed after stop is rejected");

    section("NULL stream is tolerated");
    check(edge_stream_feed(NULL, 10) == EDGE_ERR_INVALID_ARG, "feed(NULL)");
    check(edge_stream_reward_event(NULL, 0, 0) == EDGE_ERR_INVALID_ARG, "reward_event(NULL)");
    check(edge_stream_stop(NULL, 1) == EDGE_ERR_INVALID_ARG, "stop(NULL)");

    section("destroying a handle with a live stream is safe");
    check(edge_glasses_start_feedback_stream(g, 45.0, &s) == EDGE_OK, "reopened");
    edge_stream_feed(s, 30);
    edge_glasses_destroy(g); /* must join the writer thread before teardown */
    check(1, "destroyed without crashing");
}

int main(void) {
    printf("EDGE Glasses C API - test suite\n");
    printf("===============================\n");

    test_frame_builders();
    test_controller();
    test_stream();

    printf("\n-------------------------------\n");
    printf("%d checks, %d failed\n", g_checks, g_failures);
    if (g_failures == 0) printf("ALL TESTS PASSED\n");
    return g_failures == 0 ? 0 : 1;
}
