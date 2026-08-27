/* SPDX-License-Identifier: MIT
 *
 * EDGE Glasses — C API.
 *
 * A flat C89-compatible ABI over the C++ SDK, for pure-C callers and for
 * P/Invoke-style bindings from other languages. Nothing here throws; every
 * fallible call returns an edge_status and leaves a human-readable message in
 * edge_last_error().
 *
 * Two independent tiers — use either or both:
 *
 *   1. Frame builders (edge_frame_*). Pure functions, no state, no BLE. They
 *      write the exact bytes the firmware expects into your buffer, with all
 *      the clamping and the >= 2-byte padding rule applied. Use these if your
 *      application already owns its Bluetooth stack:
 *
 *          uint8_t buf[EDGE_MAX_FRAME];
 *          int n = edge_frame_set_static(buf, sizeof buf, 50);
 *          my_gatt_write(EDGE_UUID_CONTROL, buf, (size_t)n);
 *
 *   2. Controller (edge_glasses_*). A handle that owns command sequencing, the
 *      real-time feedback stream, and — on Windows — the BLE connection itself:
 *
 *          edge_glasses g;
 *          edge_glasses_create_winrt(&g);
 *          edge_glasses_connect(g, 10000);
 *          edge_glasses_set_duration(g, 60);
 *          edge_glasses_set_static(g, 50);
 *          edge_glasses_destroy(g);
 *
 * Thread safety: every edge_glasses_* call is serialized internally, so the
 * protocol's "never overlap writes to 0xFF01" rule holds even when a feedback
 * stream and your own calls run concurrently. A handle must not be destroyed
 * while another thread is using it.
 */

#ifndef EDGE_GLASSES_H
#define EDGE_GLASSES_H

#include <stddef.h>

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned char uint8_t;
typedef __int64 int64_t;
#else
#include <stdint.h>
#endif

#if defined(_WIN32) && defined(EDGE_GLASSES_SHARED)
#if defined(EDGE_GLASSES_BUILDING)
#define EDGE_API __declspec(dllexport)
#else
#define EDGE_API __declspec(dllimport)
#endif
#else
#define EDGE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** Exact advertised device name. The service UUID is not advertised — match this. */
#define EDGE_DEVICE_NAME "Narbis_Edge"

/** Custom service and its characteristics. */
#define EDGE_UUID_SERVICE  "000000ff-0000-1000-8000-00805f9b34fb"
#define EDGE_UUID_CONTROL  "0000ff01-0000-1000-8000-00805f9b34fb"
#define EDGE_UUID_OTA_DATA "0000ff02-0000-1000-8000-00805f9b34fb"
#define EDGE_UUID_STATUS   "0000ff03-0000-1000-8000-00805f9b34fb"
#define EDGE_UUID_PPG      "0000ff04-0000-1000-8000-00805f9b34fb"

/** Standard BLE Battery Service (firmware >= 4.16.1 on V1.2+ hardware). */
#define EDGE_UUID_BATTERY_SERVICE "0000180f-0000-1000-8000-00805f9b34fb"
#define EDGE_UUID_BATTERY_LEVEL   "00002a19-0000-1000-8000-00805f9b34fb"

/** Largest frame any edge_frame_* builder produces. */
#define EDGE_MAX_FRAME 24

/** Breathe waveform shape (opcode 0xB5). */
typedef enum {
    EDGE_WAVEFORM_SINE = 0,
    EDGE_WAVEFORM_LINEAR = 1
} edge_waveform;

/** Result of a fallible call. Negative values are errors. */
typedef enum {
    EDGE_OK = 0,
    EDGE_ERR_INVALID_ARG = -1,      /**< null handle or bad buffer */
    EDGE_ERR_NOT_CONNECTED = -2,    /**< no link; call edge_glasses_connect first */
    EDGE_ERR_DEVICE_NOT_FOUND = -3, /**< no "Narbis_Edge" found — magnet-tap to wake */
    EDGE_ERR_CONNECTION = -4,       /**< the connection attempt failed */
    EDGE_ERR_COMMAND = -5,          /**< the GATT write did not reach the device */
    EDGE_ERR_TIMEOUT = -6,          /**< the operation exceeded its timeout */
    EDGE_ERR_UNSUPPORTED = -7,      /**< not available in this build (e.g. WinRT off Windows) */
    EDGE_ERR_INTERNAL = -99
} edge_status;

/** Opaque controller handle. */
typedef struct edge_glasses_s* edge_glasses;
/** Opaque real-time feedback stream handle. */
typedef struct edge_stream_s* edge_stream;

/* ------------------------------------------------------------------------ */
/* Errors and version                                                        */
/* ------------------------------------------------------------------------ */

/**
 * Message for the most recent failure on the calling thread.
 * Valid until the next failing call on that thread; never NULL.
 */
EDGE_API const char* edge_last_error(void);

/** SDK version string, e.g. "2.5.0". */
EDGE_API const char* edge_version(void);

/** Human-readable name for a status code, e.g. "EDGE_ERR_NOT_CONNECTED". */
EDGE_API const char* edge_status_name(edge_status status);

/* ------------------------------------------------------------------------ */
/* Tier 1 — pure frame builders (no BLE, no state)                           */
/*                                                                           */
/* Each writes the frame into `out` and returns the byte count, or a negative */
/* edge_status if `out` is NULL or `cap` is too small. All arguments are      */
/* clamped: the firmware never NACKs, so client-side clamping is what makes   */
/* "what you send is what runs" true.                                         */
/* ------------------------------------------------------------------------ */

/**
 * Legacy opacity write — ONE byte, 0 (clear) .. 255 (fully dark).
 *
 * The firmware reads any single-byte write as a direct lens duty and stops
 * whatever mode is running. This is the only frame allowed to be 1 byte long;
 * every other command must be at least 2 bytes.
 */
EDGE_API int edge_frame_opacity(uint8_t* out, size_t cap, int value);

/** Static mode at a fixed duty, 0-100 % (opcode 0xA5) — the real-time dimming command. */
EDGE_API int edge_frame_set_static(uint8_t* out, size_t cap, int duty);

/** Persistent max-tint / breathe depth, 0-100 % (opcode 0xA2). */
EDGE_API int edge_frame_set_brightness(uint8_t* out, size_t cap, int percent);

/** Session duration, 1-60 minutes (opcode 0xA4). Device auto-sleeps at expiry. */
EDGE_API int edge_frame_set_duration(uint8_t* out, size_t cap, int minutes);

/** Strobe frequency, 1-50 Hz (opcode 0xAB). */
EDGE_API int edge_frame_set_strobe_frequency(uint8_t* out, size_t cap, int hz);

/** Strobe dark-phase duty, 10-90 % (opcode 0xAC). */
EDGE_API int edge_frame_set_strobe_duty(uint8_t* out, size_t cap, int percent);

/** On-device lens smoothing, 0-2550 ms in 10 ms steps (opcode 0xA0, fw >= 4.15.7). */
EDGE_API int edge_frame_set_lens_smoothing(uint8_t* out, size_t cap, int ms);

/** Lens slew cap, 0-100 %/100 ms; 0 = unlimited (opcode 0xA1, fw >= 4.15.7). */
EDGE_API int edge_frame_set_lens_max_rate(uint8_t* out, size_t cap, int percent_per_100ms);

/** On-disconnect behaviour: 0 = freeze at last tint, 1 = fail clear (opcode 0xA3). */
EDGE_API int edge_frame_set_disconnect_behavior(uint8_t* out, size_t cap, int fail_clear);

/** Enter strobe mode (opcode 0xA6), padded to 2 bytes. */
EDGE_API int edge_frame_start_strobe(uint8_t* out, size_t cap);

/** Breathe rate, 1-30 BPM (opcode 0xB1). Use edge_frame_sync_breath for fractional rates. */
EDGE_API int edge_frame_set_breathe_bpm(uint8_t* out, size_t cap, int bpm);

/** Inhale portion of the breath cycle, 10-90 % (opcode 0xB2). */
EDGE_API int edge_frame_set_breathe_inhale_pct(uint8_t* out, size_t cap, int percent);

/** Hold at full-dark, 0-5000 ms in 100 ms steps (opcode 0xB3). */
EDGE_API int edge_frame_set_breathe_hold_top(uint8_t* out, size_t cap, int ms);

/** Hold at clear, 0-5000 ms in 100 ms steps (opcode 0xB4). */
EDGE_API int edge_frame_set_breathe_hold_bottom(uint8_t* out, size_t cap, int ms);

/** Breathe waveform shape (opcode 0xB5). */
EDGE_API int edge_frame_set_breathe_waveform(uint8_t* out, size_t cap, edge_waveform waveform);

/** Enter breathe mode (opcode 0xB0). with_strobe != 0 selects breathe+strobe (fw >= 4.15.6). */
EDGE_API int edge_frame_start_breathe(uint8_t* out, size_t cap, int with_strobe);

/**
 * Phase-lock the breathe engine (opcode 0xBA, fw >= 4.15.5).
 * Wire format [0xBA, cycle_lo, cycle_hi, inhale_pct] — cycle length as u16 LE.
 * Send ONLY at the breath-cycle boundary; re-send each breath to hold the rate.
 */
EDGE_API int edge_frame_sync_breath(uint8_t* out, size_t cap, int cycle_ms, int inhale_pct);

/** Enter deep sleep now (opcode 0xA7). */
EDGE_API int edge_frame_sleep(uint8_t* out, size_t cap);

/** Reset persisted settings to factory defaults (opcode 0xBF). */
EDGE_API int edge_frame_factory_reset(uint8_t* out, size_t cap);

/** Battery poll: reprobe = 0 refresh, 1 re-probe (opcode 0xC7, fw >= 4.16.1). */
EDGE_API int edge_frame_battery_poll(uint8_t* out, size_t cap, int reprobe);

/**
 * Arbitrary opcode frame, padded to the 2-byte minimum.
 * Use for opcodes the typed builders do not cover — e.g. the 3-byte deci-Hz
 * strobe form [0xAB, dHz_lo, dHz_hi] for sub-Hz entrainment targets.
 * @param payload may be NULL when payload_len is 0
 */
EDGE_API int edge_frame_command(uint8_t* out, size_t cap, int opcode, const uint8_t* payload,
                                size_t payload_len);

/* ------------------------------------------------------------------------ */
/* Tier 2 — controller                                                       */
/* ------------------------------------------------------------------------ */

/**
 * Write a frame to the control characteristic 0xFF01.
 * @param user the pointer passed to edge_glasses_create_with_callbacks
 * @param with_response non-zero for write-with-response
 * @return 0 on success, non-zero if the write did not reach the device
 */
typedef int (*edge_write_cb)(void* user, const uint8_t* data, size_t len, int with_response);

/** @return non-zero while the GATT link is up. */
typedef int (*edge_connected_cb)(void* user);

/**
 * Read Battery Level (0x2A19) from Battery Service (0x180F).
 * @return 1 and set *out_level (0-100) on success; 0 if the service is absent;
 *         negative on read failure.
 */
typedef int (*edge_battery_cb)(void* user, int* out_level);

/**
 * Create a controller over your own Bluetooth stack.
 *
 * The SDK supplies protocol encoding, clamping, write serialization and the
 * feedback stream; your callbacks move the bytes. Neither callback may be NULL.
 * `user` is passed back to them untouched.
 */
EDGE_API edge_status edge_glasses_create_with_callbacks(edge_write_cb write_fn,
                                                        edge_connected_cb connected_fn, void* user,
                                                        edge_glasses* out_handle);

/**
 * Declare that 0xFF01 advertises write-without-response on this link
 * (firmware >= 4.16.3), letting the streaming path skip per-write ATT acks.
 * Off by default, which is always safe. Callback-backed handles only.
 */
EDGE_API edge_status edge_glasses_set_fast_write(edge_glasses handle, int supported);

/** Supply a battery reader for a callback-backed handle. Optional. */
EDGE_API edge_status edge_glasses_set_battery_callback(edge_glasses handle, edge_battery_cb fn);

/**
 * Create a controller backed by the built-in Windows (WinRT) Bluetooth transport.
 * @return EDGE_ERR_UNSUPPORTED if this SDK was built without WinRT support.
 */
EDGE_API edge_status edge_glasses_create_winrt(edge_glasses* out_handle);

/** Destroy a handle. Stops any feedback stream first. Safe on NULL. */
EDGE_API void edge_glasses_destroy(edge_glasses handle);

/**
 * Open the link (built-in transports only; a no-op for callback-backed handles).
 *
 * The glasses power their radio down after 2 minutes with no client connected.
 * On EDGE_ERR_DEVICE_NOT_FOUND, ask the user to tap the magnet to the temple to
 * re-arm advertising, then retry.
 */
EDGE_API edge_status edge_glasses_connect(edge_glasses handle, int timeout_ms);

/** Close the link. The lens FREEZES at its last tint unless fail-clear is set. */
EDGE_API edge_status edge_glasses_disconnect(edge_glasses handle);

/** @return non-zero while connected. */
EDGE_API int edge_glasses_is_connected(edge_glasses handle);

/** @return non-zero if 0xFF01 advertises write-without-response (fw >= 4.16.3). */
EDGE_API int edge_glasses_supports_fast_write(edge_glasses handle);

/* Commands — each mirrors the identically named frame builder. */

EDGE_API edge_status edge_glasses_set_opacity(edge_glasses handle, int value);
EDGE_API edge_status edge_glasses_clear(edge_glasses handle);
EDGE_API edge_status edge_glasses_dark(edge_glasses handle);
EDGE_API edge_status edge_glasses_set_static(edge_glasses handle, int duty);
EDGE_API edge_status edge_glasses_set_brightness(edge_glasses handle, int percent);
EDGE_API edge_status edge_glasses_set_duration(edge_glasses handle, int minutes);
EDGE_API edge_status edge_glasses_set_strobe_frequency(edge_glasses handle, int hz);
EDGE_API edge_status edge_glasses_set_strobe_duty(edge_glasses handle, int percent);
EDGE_API edge_status edge_glasses_set_lens_smoothing(edge_glasses handle, int ms);
EDGE_API edge_status edge_glasses_set_lens_max_rate(edge_glasses handle, int percent_per_100ms);
EDGE_API edge_status edge_glasses_set_disconnect_behavior(edge_glasses handle, int fail_clear);

/**
 * Start strobe mode, optionally setting frequency and duty first.
 * Pass a negative value to leave a parameter at its persisted setting.
 */
EDGE_API edge_status edge_glasses_start_strobe(edge_glasses handle, int hz, int duty_pct);

/**
 * Start breathe mode, writing only the parameters you supply.
 * Pass a negative value for any parameter to leave it at its persisted setting;
 * pass waveform < 0 to leave the waveform alone.
 */
EDGE_API edge_status edge_glasses_start_breathe(edge_glasses handle, int bpm, int inhale_pct,
                                                int hold_top_ms, int hold_bottom_ms, int waveform,
                                                int with_strobe);

/** Phase-lock the breathe engine. Send only at the breath-cycle boundary. */
EDGE_API edge_status edge_glasses_sync_breath(edge_glasses handle, int cycle_ms, int inhale_pct);

EDGE_API edge_status edge_glasses_sleep(edge_glasses handle);
EDGE_API edge_status edge_glasses_factory_reset(edge_glasses handle);

/**
 * Read the battery charge level.
 * @param out_level receives 0-100 on success
 * @return EDGE_OK, or EDGE_ERR_UNSUPPORTED if the unit exposes no 0x180F
 *         Battery Service (pre-4.16.1 firmware, or a board without the divider)
 */
EDGE_API edge_status edge_glasses_get_battery(edge_glasses handle, int* out_level);

/** Send a raw opcode, padded to the 2-byte minimum. payload may be NULL. */
EDGE_API edge_status edge_glasses_send_command(edge_glasses handle, int opcode,
                                               const uint8_t* payload, size_t payload_len);

/* Preset sessions — fixed-parameter; nothing ramps over the session. */

EDGE_API edge_status edge_glasses_session_relax(edge_glasses handle, int minutes);
EDGE_API edge_status edge_glasses_session_meditate(edge_glasses handle, int minutes);
EDGE_API edge_status edge_glasses_session_focus(edge_glasses handle, int minutes);
EDGE_API edge_status edge_glasses_session_sleep(edge_glasses handle, int minutes);

/* ------------------------------------------------------------------------ */
/* Real-time feedback stream                                                 */
/* ------------------------------------------------------------------------ */

/**
 * Open a real-time lens stream — the wearable screen-dimmer pattern.
 *
 * Push values from any thread at any rate with edge_stream_feed(); a background
 * writer updates the lens at rate_hz (clamped to 1-45), coalescing unchanged
 * values and keeping exactly one write in flight.
 *
 * One stream per handle: opening a second stops the first.
 */
EDGE_API edge_status edge_glasses_start_feedback_stream(edge_glasses handle, double rate_hz,
                                                        edge_stream* out_stream);

/** Request a lens duty, 0 (clear) .. 100 (fully dark). Cheap; call at any rate. */
EDGE_API edge_status edge_stream_feed(edge_stream stream, int duty);

/** Request tint from a 0..1 reward value (1 = in condition = clear). */
EDGE_API edge_status edge_stream_feed_reward(edge_stream stream, double value);

/**
 * Deliver a DISCRETE reward now, bypassing the stream tick.
 *
 * For operant conditioning: call the instant your detector crosses threshold.
 * Latency is just the BLE transport, with no cadence jitter.
 *
 * @param duty reward tint 0-100 (0 = fully clear = positive reward)
 * @param hold_ms hold the reward tint this long before the stream resumes
 */
EDGE_API edge_status edge_stream_reward_event(edge_stream stream, int duty, int hold_ms);

/**
 * Stop the writer.
 * @param clear_lens non-zero clears the lens; it otherwise FREEZES at the last tint.
 * The stream handle is invalid after this call.
 */
EDGE_API edge_status edge_stream_stop(edge_stream stream, int clear_lens);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* EDGE_GLASSES_H */
