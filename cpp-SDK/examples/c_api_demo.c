/* SPDX-License-Identifier: MIT
 *
 * EDGE Glasses from pure C.
 *
 * Two halves, matching the two tiers of edge_glasses.h:
 *
 *   1. Frame builders. No BLE, no handles - just the exact bytes to write to
 *      characteristic 0xFF01. Use these if your application already owns its
 *      Bluetooth stack, which is the common case for an existing C codebase.
 *
 *   2. The controller. A handle that owns command sequencing and the real-time
 *      feedback stream, driven either by the built-in Windows transport or by
 *      two callbacks into your own stack.
 */

#include <stdio.h>
#include <string.h>

#include "edge/edge_glasses.h"

#if defined(_WIN32)
#include <windows.h>
static void demo_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void demo_sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
#endif

/* ------------------------------------------------------------------------ */
/* Part 1 - frame builders: bring your own BLE stack                         */
/* ------------------------------------------------------------------------ */

static void print_frame(const char* label, const uint8_t* frame, int n) {
    int i;
    if (n < 0) {
        printf("  %-28s ERROR %s (%s)\n", label, edge_status_name((edge_status)n),
               edge_last_error());
        return;
    }
    printf("  %-28s", label);
    for (i = 0; i < n; ++i) printf(" %02X", frame[i]);
    printf("\n");
}

static void demo_frames(void) {
    uint8_t frame[EDGE_MAX_FRAME];

    printf("Frame builders - write these bytes to characteristic 0xFF01:\n");
    print_frame("session guard, 60 min", frame, edge_frame_set_duration(frame, sizeof frame, 60));
    print_frame("static 50 %", frame, edge_frame_set_static(frame, sizeof frame, 50));
    print_frame("static 100 % (dark)", frame, edge_frame_set_static(frame, sizeof frame, 100));
    print_frame("legacy opacity 128", frame, edge_frame_opacity(frame, sizeof frame, 128));
    print_frame("smoothing 80 ms", frame,
                edge_frame_set_lens_smoothing(frame, sizeof frame, 80));
    print_frame("breathe 6 BPM", frame, edge_frame_set_breathe_bpm(frame, sizeof frame, 6));
    print_frame("enter breathe", frame, edge_frame_start_breathe(frame, sizeof frame, 0));
    print_frame("sync 5500 ms / 40 %", frame,
                edge_frame_sync_breath(frame, sizeof frame, 5500, 40));
    print_frame("sleep", frame, edge_frame_sleep(frame, sizeof frame));

    /* Out-of-range values are clamped, because the firmware never NACKs. */
    print_frame("static 999 -> clamped", frame, edge_frame_set_static(frame, sizeof frame, 999));

    /* Anything the typed builders do not cover goes through command(), which
       still enforces the >= 2-byte rule. Here: the 3-byte deci-Hz strobe form
       for a 13.5 Hz entrainment target. */
    {
        uint8_t dhz[2];
        int deci = 135; /* 13.5 Hz x 10 */
        dhz[0] = (uint8_t)(deci & 0xFF);
        dhz[1] = (uint8_t)((deci >> 8) & 0xFF);
        print_frame("strobe 13.5 Hz (deci-Hz)", frame,
                    edge_frame_command(frame, sizeof frame, 0xAB, dhz, 2));
    }
}

/* ------------------------------------------------------------------------ */
/* Part 2 - the controller over your own BLE stack                           */
/* ------------------------------------------------------------------------ */

/* Stand-in for your GATT layer. Replace the body with your real write. */
static int my_gatt_write(void* user, const uint8_t* data, size_t len, int with_response) {
    size_t i;
    (void)user;
    printf("    -> 0xFF01 (%s)", with_response ? "with response" : "no response");
    for (i = 0; i < len; ++i) printf(" %02X", data[i]);
    printf("\n");
    return 0; /* non-zero would surface as EDGE_ERR_COMMAND */
}

static int my_is_connected(void* user) {
    (void)user;
    return 1;
}

static void demo_controller(void) {
    edge_glasses glasses = NULL;
    edge_stream stream = NULL;
    edge_status status;

    printf("\nController over a caller-supplied BLE stack:\n");

    status = edge_glasses_create_with_callbacks(my_gatt_write, my_is_connected, NULL, &glasses);
    if (status != EDGE_OK) {
        printf("  create failed: %s (%s)\n", edge_status_name(status), edge_last_error());
        return;
    }

    /* Firmware >= 4.16.3 advertises write-without-response on 0xFF01. The
       built-in Windows transport detects this; with your own stack, declare it
       after checking the characteristic's properties. */
    edge_glasses_set_fast_write(glasses, 0);

    printf("  session guard:\n");
    edge_glasses_set_duration(glasses, 60);
    printf("  smoothing + slew cap:\n");
    edge_glasses_set_lens_smoothing(glasses, 80);
    edge_glasses_set_lens_max_rate(glasses, 40);
    printf("  half tint:\n");
    edge_glasses_set_static(glasses, 50);

    printf("  feedback stream:\n");
    status = edge_glasses_start_feedback_stream(glasses, 30.0, &stream);
    if (status == EDGE_OK) {
        /* In your app these come from your pipeline's callback, at any rate.
           feed_reward() parks the value for the next writer tick, so wait one
           tick to see it reach the wire. */
        edge_stream_feed_reward(stream, 0.25); /* 25 % in condition -> 75 % tint */
        demo_sleep_ms(100);

        /* A discrete reward does NOT wait for a tick - it writes immediately. */
        edge_stream_reward_event(stream, 0, 200);
        edge_stream_stop(stream, 1); /* stop and clear the lens */
    } else {
        printf("    stream failed: %s\n", edge_status_name(status));
    }

    edge_glasses_destroy(glasses);
}

/* ------------------------------------------------------------------------ */

int main(void) {
    printf("EDGE Glasses C API demo (SDK %s)\n", edge_version());
    printf("=====================================\n\n");

    demo_frames();
    demo_controller();

    printf("\nOn Windows, edge_glasses_create_winrt() gives you the same handle\n"
           "backed by the built-in Bluetooth transport - no callbacks needed.\n");
    return 0;
}
