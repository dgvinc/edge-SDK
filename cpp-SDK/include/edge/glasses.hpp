// SPDX-License-Identifier: MIT
//
// EDGE Glasses — device controller.
//
// Mirrors the Python (edge_glasses.Glasses) and JavaScript (Glasses) SDKs
// method for method, so a protocol written against one ports to another by
// renaming. Calls are blocking: each returns once the BLE write has completed.
//
//     edge::WinRtTransport t;
//     edge::Glasses g(&t);
//     g.connect();
//     g.set_duration(60);      // session guard — no auto-sleep for 60 min
//     g.set_static(50);        // 50 % tint
//
// All biofeedback processing runs app-side: the glasses are a display.
// Configure and start the firmware's breathe / static / strobe renderer, or
// stream static-duty writes for continuous feedback.

#ifndef EDGE_GLASSES_HPP
#define EDGE_GLASSES_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "edge/errors.hpp"
#include "edge/protocol.hpp"
#include "edge/transport.hpp"

namespace edge {

class FeedbackStream;

/// Optional breathe parameters. Only the fields you set are written; everything
/// else keeps its current (NVS-persisted) value on the device.
struct BreatheOptions {
    std::optional<int> bpm;             ///< 1-30 BPM (integer; use sync_breath for fractional)
    std::optional<int> inhale_pct;      ///< inhale portion of the cycle, 10-90 %
    std::optional<int> hold_top_ms;     ///< hold at full-dark, 0-5000 ms (100 ms resolution)
    std::optional<int> hold_bottom_ms;  ///< hold at clear, 0-5000 ms (100 ms resolution)
    std::optional<Waveform> waveform;   ///< Sine or Linear
    bool with_strobe = false;           ///< true = breathe+strobe (fw >= 4.15.6)
};

/// EDGE Smart Glasses controller.
///
/// The firmware never NACKs a command — bad arguments are silently clamped or
/// dropped on the device. This SDK clamps every argument client-side, so what
/// you send is what runs.
///
/// Thread-safe: every write is serialized on an internal mutex, satisfying the
/// protocol's "never overlap writes to 0xFF01" rule even when a FeedbackStream
/// writer thread and your own calls are both active.
class Glasses {
public:
    /// Wrap a transport this object does not own. The transport must outlive
    /// the Glasses instance.
    explicit Glasses(ITransport* transport);

    /// Wrap a transport this object takes ownership of.
    explicit Glasses(std::unique_ptr<ITransport> transport);

    ~Glasses();

    Glasses(const Glasses&) = delete;
    Glasses& operator=(const Glasses&) = delete;

    // -----------------------------------------------------------------------
    // Connection
    // -----------------------------------------------------------------------

    /// Open the link, for transports that own their connection (WinRtTransport).
    ///
    /// The glasses stop advertising and fully power down the radio after
    /// 2 minutes with no client connected. If the device cannot be found, tap
    /// the magnet to the temple briefly to wake it and re-arm advertising,
    /// then retry.
    ///
    /// @param timeout_ms overall budget for scan + connect
    /// @throws DeviceNotFoundError, ConnectionError, TimeoutError
    void connect(int timeout_ms = 10000);

    /// Close the link. Never throws.
    ///
    /// The lens FREEZES at its last commanded tint across a disconnect unless
    /// you enabled set_disconnect_behavior(true) — send clear() first if the
    /// wearer should not be left dark.
    void disconnect();

    /// True while connected.
    bool is_connected() const;

    /// True if the control characteristic advertises write-without-response.
    ///
    /// Firmware >= 4.16.3 exposes write-without-response on 0xFF01, letting the
    /// real-time streaming path skip the ATT round-trip per write for higher
    /// sustained throughput. Older firmware is write-with-response only, and
    /// this stays false.
    bool supports_fast_write() const;

    // -----------------------------------------------------------------------
    // Low-level
    // -----------------------------------------------------------------------

    /// Send a low-level opcode command.
    ///
    /// Pads the total write to >= 2 bytes (a 1-byte write is the legacy opacity
    /// command). The firmware never NACKs — invalid opcodes or arguments are
    /// silently dropped or clamped on the device.
    ///
    ///     g.send_command(0xA2, {80});   // brightness 80 %
    ///     g.send_command(0xA7);         // sleep (padded to [0xA7, 0x00])
    void send_command(int opcode, const std::vector<std::uint8_t>& payload = {});

    /// Write an already-built protocol frame with write-with-response.
    void send_frame(const protocol::Frame& frame);

    // -----------------------------------------------------------------------
    // Opacity (legacy single-byte write)
    // -----------------------------------------------------------------------

    /// Set lens opacity via the legacy single-byte write.
    ///
    /// Intentionally sends a single byte — the firmware treats any 1-byte write
    /// as a direct opacity command (0-255 -> 0-100 % static duty). Stops
    /// whatever mode is currently running.
    ///
    /// Stream this for continuous real-time feedback. There is no 20 Hz
    /// protocol ceiling; target a configurable 30-50 Hz band with coalescing on.
    /// The BLE link paces you — write-with-response keeps exactly one write in
    /// flight, so the effective rate self-limits to your data rate (~8-11
    /// writes/sec on default connection parameters, ~20/sec with
    /// throughput-optimized params).
    ///
    /// @param value 0 = clear .. 255 = fully dark (clamped)
    void set_opacity(int value);

    /// Fully clear (transparent) lenses.
    void clear();

    /// Fully dark (opaque) lenses.
    void dark();

    // -----------------------------------------------------------------------
    // Parameters
    // -----------------------------------------------------------------------

    /// Enter static mode at a fixed duty (0xA5) — the primary real-time dimming
    /// command. Stops the current mode and holds the lens at the given tint.
    ///
    /// On firmware >= 4.16.2 this is a clean static-duty write that does NOT
    /// touch the 0xA2 brightness / breathe depth, so you can stream dimming to 0
    /// without zeroing the depth of the other programs.
    ///
    /// Duty 1-100 maps to a perceptual floor on the device (raw 265-1023);
    /// 0 is the only fully-clear value.
    ///
    /// @param duty 0-100 % (clamped)
    void set_static(int duty);

    /// Set the persistent max-tint / breathe depth (0xA2). Persisted in NVS.
    /// Does not change mode. This is the master tint level that MULTIPLIES the
    /// breathe / strobe / static output.
    ///
    /// On firmware >= 4.16.2 this is decoupled from set_static(). On firmware
    /// <= 4.16.1 the two shared one variable, so a set_static(0) left brightness
    /// at 0 and later breathe/strobe rendered clear until 0xA2 was re-sent.
    ///
    /// @param percent 0-100 % (clamped)
    void set_brightness(int percent);

    /// Set session duration (0xA4). Persisted in NVS; the device auto-sleeps
    /// when the session ends. The clock runs from device wake, so writing this
    /// changes the total but does not restart it.
    /// @param minutes 1-60 (clamped)
    void set_duration(int minutes);

    /// Set strobe frequency (0xAB). Persisted in NVS. Takes effect immediately
    /// if strobing. @param hz 1-50 Hz (clamped)
    void set_strobe_frequency(int hz);

    /// Set strobe dark-phase duty (0xAC). Persisted in NVS.
    /// @param percent 10-90 % (clamped)
    void set_strobe_duty(int percent);

    // -----------------------------------------------------------------------
    // Lens config (firmware >= 4.15.7; older firmware ignores these)
    // -----------------------------------------------------------------------

    /// Set on-device lens smoothing (0xA0). Persisted in NVS.
    ///
    /// The firmware glides between commanded static targets with an EMA of this
    /// time constant, so the lens moves continuously between your writes instead
    /// of stepping — the recommended way to get smooth real-time feedback and to
    /// absorb per-sample noise without filtering client-side. Send it once at
    /// connect. Rule of thumb: tau ~= 1-2x your write period (a 30 Hz stream is
    /// a ~33 ms period -> ~35-70 ms; ~80 ms is a good general value).
    ///
    /// For a CONTINUOUS stream use firmware >= 4.15.9: 4.15.7 stalls ~2-4 %
    /// short of each target (fixed in 4.15.8), and through 4.15.8 the smoothed
    /// output was still floored to ~101 duty levels; 4.15.9 drives the lens at
    /// full 10-bit PWM resolution. One-shot writes are fine on any firmware.
    ///
    /// @param ms time constant 0-2550 ms, 10 ms resolution. 0 = off (snap).
    void set_lens_smoothing(int ms);

    /// Cap how fast the lens may transition (0xA1). Persisted in NVS.
    ///
    /// A hard slew limit on commanded static transitions, applied after the
    /// smoothing glide — a safety envelope guaranteeing the lens cannot snap
    /// even if a host streams garbage. 40 corresponds to full-scale in ~250 ms.
    /// Does not affect breathe/strobe waveforms.
    ///
    /// @param percent_per_100ms 0-100 %/100 ms. 0 = unlimited (factory default).
    void set_lens_max_rate(int percent_per_100ms);

    /// Choose what the lens does when the BLE link drops (0xA3). Persisted in NVS.
    ///
    /// Factory default (false): the lens FREEZES at its last commanded output
    /// across a disconnect — a crashed app leaves the last tint in place. With
    /// true the glasses instead stop any strobe and drop to a clear static lens
    /// on link loss (riding the set_lens_smoothing glide if configured).
    ///
    /// The failsafe fires when the firmware declares the link dead, bounded by
    /// the ~32 s supervision timeout — still send an explicit clear() before an
    /// intentional disconnect.
    void set_disconnect_behavior(bool fail_clear);

    // -----------------------------------------------------------------------
    // Modes
    // -----------------------------------------------------------------------

    /// Start strobe mode (0xA6), optionally writing frequency and duty first.
    /// Omitted parameters keep their current (NVS-persisted) values.
    /// @param hz optional 1-50 Hz
    /// @param duty_pct optional dark-phase duty 10-90 %
    void start_strobe(std::optional<int> hz = std::nullopt,
                      std::optional<int> duty_pct = std::nullopt);

    /// Start breathe mode (0xB0), writing only the parameters you pass.
    /// Omitted parameters keep their current (NVS-persisted) values.
    /// With options.with_strobe the strobe's dark-phase duty is modulated by
    /// the breathing waveform (firmware >= 4.15.6).
    void start_breathe(const BreatheOptions& options = {});

    /// Phase-lock the breathe engine to an app-paced cycle (0xBA).
    ///
    /// Restarts the breathe cosine at the instant of the write and sets the
    /// EXACT cycle length in milliseconds — this is how you get fractional
    /// breathing rates (0xB1 is integer-BPM only). Requires firmware >= 4.15.5;
    /// older firmware ignores it, so it is always safe to send.
    ///
    /// IMPORTANT: send this only at the breath-cycle boundary (the start of an
    /// inhale), never mid-breath — the engine restarts its waveform immediately
    /// on receipt. The sync auto-expires 2 cycles after the last write,
    /// reverting to the stored integer-BPM rate, so re-send once per breath to
    /// stay locked.
    ///
    /// @param cycle_ms full breath cycle length in ms (e.g. 5500 for 10.9 BPM)
    /// @param inhale_pct inhale portion of the cycle, 10-90 %
    void sync_breath(int cycle_ms, int inhale_pct = 40);

    // -----------------------------------------------------------------------
    // Power / maintenance
    // -----------------------------------------------------------------------

    /// Put the glasses into deep sleep now (0xA7). Wake with a magnet tap.
    void sleep();

    /// Reset all NVS-persisted settings to factory defaults (0xBF).
    void factory_reset();

    /// Read the battery charge level over the standard BLE Battery Service.
    ///
    /// Reads Battery Level (0x2A19) from the Battery Service (0x180F), which
    /// firmware >= 4.16.1 exposes on V1.2+ hardware.
    ///
    /// @return charge 0-100 %, or std::nullopt if the device does not expose
    ///         0x180F (pre-4.16.1 firmware, or a board built without the
    ///         battery divider).
    ///
    /// Note: on firmware >= 4.16.1 the 0x2A19 characteristic is registered on
    /// every unit but reads 0 while the level is unknown — it cannot distinguish
    /// "unknown" from a genuine 0 %. To tell those apart (or to get millivolts
    /// and a charging flag) read the 0xFB status frame on 0xFF03 instead:
    /// [mv:u16 LE][soc:u8][charging:u8], where soc = 0xFF means unknown.
    std::optional<int> get_battery();

    // -----------------------------------------------------------------------
    // Preset sessions
    // -----------------------------------------------------------------------
    // Fixed-parameter presets: the firmware no longer ramps any parameter over
    // the session, so each preset just configures the breathe/strobe engine and
    // sets the auto-sleep duration.

    /// Relaxation: 5 BPM sine breathing at full brightness.
    void session_relax(int duration_minutes = 10);

    /// Meditation: 6 BPM sine breathing (the device default).
    void session_meditate(int duration_minutes = 10);

    /// Focus: breathe+strobe, 12 Hz strobe modulated by 8 BPM breathing.
    void session_focus(int duration_minutes = 10);

    /// Sleep preparation: 4 BPM sine breathing.
    void session_sleep(int duration_minutes = 15);

    // -----------------------------------------------------------------------
    // Real-time feedback streaming
    // -----------------------------------------------------------------------

    /// Open a plug-and-play real-time lens stream (the screen-dimmer pattern).
    ///
    /// Push a value from any thread at any rate via feed() / feed_reward();
    /// a background writer updates the lens at rate_hz, coalescing unchanged
    /// values and keeping exactly one write in flight. Replaces a hand-rolled
    /// decimate/coalesce/serialize loop. The rate is a target, never a queue.
    ///
    /// Proportional feedback (a dimmer that tracks your signal) uses feed() /
    /// feed_reward(); discrete operant rewards use reward_event(), which fires
    /// immediately instead of waiting for the next tick.
    ///
    /// @param rate_hz writer rate, clamped to 1-45 Hz
    /// @return the stream; stop() is called automatically on destruction
    std::unique_ptr<FeedbackStream> start_feedback_stream(double rate_hz = 30.0);

    /// Fast static-duty write for the real-time streaming path (0xA5).
    ///
    /// Uses write-without-response when the control characteristic advertises it
    /// (firmware >= 4.16.3), which lifts sustained throughput past the ~20/sec
    /// that per-write acks allow; otherwise falls back to write-with-response.
    /// Used by FeedbackStream. Command writes keep write-with-response for
    /// ordering and back-pressure.
    ///
    /// @param duty 0-100 % (clamped)
    void stream_static(int duty);

private:
    /// Write raw bytes with write-with-response, serialized against every other
    /// write. No padding — set_opacity() relies on the 1-byte form reaching the
    /// device intact.
    void write_raw(const std::uint8_t* data, std::size_t len);

    ITransport* transport_;
    std::unique_ptr<ITransport> owned_transport_;
    /// Serializes every write to 0xFF01 (protocol doc §4.3). Also shared with
    /// FeedbackStream's writer thread through stream_static().
    std::mutex write_mutex_;
};

}  // namespace edge

#endif  // EDGE_GLASSES_HPP
