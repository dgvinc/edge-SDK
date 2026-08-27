// SPDX-License-Identifier: MIT
//
// EDGE Glasses — wire protocol encoder (header-only, zero dependencies)
//
// This header builds the exact byte frames the Narbis Edge firmware expects.
// It has no BLE dependency at all: include it, call a builder, hand the bytes
// to whatever GATT stack you already use.
//
//     auto f = edge::protocol::set_static(50);      // 50 % tint
//     my_ble_write(CTRL_UUID, f.data(), f.size());
//
// Targets glasses firmware 4.15.6+ (device name "Narbis_Edge"). The lens-config
// frames (0xA0/0xA1/0xA3) need firmware 4.15.7+; older firmware ignores them, so
// they are always safe to send.
//
// The firmware NEVER NACKs a command — bad arguments are silently clamped or
// dropped on the device. Every builder below clamps client-side, so what you
// send is what runs.

#ifndef EDGE_PROTOCOL_HPP
#define EDGE_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>

namespace edge {

/// Breathe waveform shape (opcode 0xB5).
enum class Waveform : std::uint8_t {
    Sine = 0,   ///< Cosine-shaped tint curve (device default)
    Linear = 1  ///< Linear (triangle) tint curve
};

namespace uuid {

// The Edge exposes one custom service, 0x00FF, with four characteristics.
// The service UUID is NOT in the advertising payload — filter by device name.
constexpr const char* kService = "000000ff-0000-1000-8000-00805f9b34fb";
constexpr const char* kControl = "0000ff01-0000-1000-8000-00805f9b34fb";  ///< all commands
constexpr const char* kOtaData = "0000ff02-0000-1000-8000-00805f9b34fb";
constexpr const char* kStatus  = "0000ff03-0000-1000-8000-00805f9b34fb";
constexpr const char* kPpg     = "0000ff04-0000-1000-8000-00805f9b34fb";

/// Standard BLE Battery Service (firmware >= 4.16.1 on V1.2+ hardware).
constexpr const char* kBatteryService = "0000180f-0000-1000-8000-00805f9b34fb";
constexpr const char* kBatteryLevel   = "00002a19-0000-1000-8000-00805f9b34fb";

constexpr std::uint16_t kServiceShort        = 0x00FF;
constexpr std::uint16_t kControlShort        = 0xFF01;
constexpr std::uint16_t kOtaDataShort        = 0xFF02;
constexpr std::uint16_t kStatusShort         = 0xFF03;
constexpr std::uint16_t kPpgShort            = 0xFF04;
constexpr std::uint16_t kBatteryServiceShort = 0x180F;
constexpr std::uint16_t kBatteryLevelShort   = 0x2A19;

}  // namespace uuid

/// Exact advertised device name. Match it exactly — the service UUID is not advertised.
constexpr const char* kDeviceName = "Narbis_Edge";

namespace protocol {

/// Control-characteristic opcodes (0xFF01). See docs/bluetooth-protocol.md §4.3.
enum Opcode : std::uint8_t {
    kLensSmoothing       = 0xA0,  ///< EMA glide between static targets (fw >= 4.15.7)
    kLensMaxRate         = 0xA1,  ///< slew cap on static transitions (fw >= 4.15.7)
    kBrightness          = 0xA2,  ///< persistent max-tint / breathe depth
    kDisconnectBehavior  = 0xA3,  ///< freeze (0) vs fail-clear (1) on link loss (fw >= 4.15.7)
    kDuration            = 0xA4,  ///< session length, minutes (auto-sleep at end)
    kStatic              = 0xA5,  ///< static duty — the real-time feedback opcode
    kStartStrobe         = 0xA6,
    kSleep               = 0xA7,
    kStrobeFrequency     = 0xAB,
    kStrobeDuty          = 0xAC,
    kStartBreathe        = 0xB0,  ///< arg 0 = breathe, 1 = breathe+strobe (fw >= 4.15.6)
    kBreatheBpm          = 0xB1,
    kBreatheInhalePct    = 0xB2,
    kBreatheHoldTop      = 0xB3,
    kBreatheHoldBottom   = 0xB4,
    kBreatheWaveform     = 0xB5,
    kSyncBreath          = 0xBA,  ///< phase-lock + exact cycle length (fw >= 4.15.5)
    kFactoryReset        = 0xBF,
    kBatteryPoll         = 0xC7   ///< fw >= 4.16.1 on V1.2+ hardware
};

/// Largest frame this encoder ever produces. The longest documented opcode
/// payload is 0xE0 (12 B of coherence tuning) for 13 B on the wire; 24 leaves
/// headroom for a caller-supplied payload via command().
constexpr std::size_t kMaxFrameSize = 24;

/// A ready-to-write control frame. Fixed storage — never allocates.
class Frame {
public:
    constexpr Frame() noexcept : data_{}, size_(0) {}

    /// Raw bytes to write to characteristic 0xFF01.
    constexpr const std::uint8_t* data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }

    constexpr const std::uint8_t* begin() const noexcept { return data_; }
    constexpr const std::uint8_t* end() const noexcept { return data_ + size_; }
    constexpr std::uint8_t operator[](std::size_t i) const noexcept { return data_[i]; }

    /// True for the legacy single-byte opacity write, which is intentionally
    /// 1 byte and must NOT be padded (see push_back's >= 2-byte rule).
    constexpr bool is_legacy_opacity() const noexcept { return size_ == 1; }

    /// Append one byte, up to kMaxFrameSize. Extra bytes are dropped rather
    /// than overflowing — callers below never exceed the bound.
    constexpr void push_back(std::uint8_t b) noexcept {
        if (size_ < kMaxFrameSize) data_[size_++] = b;
    }

private:
    std::uint8_t data_[kMaxFrameSize];
    std::size_t size_;
};

namespace detail {

constexpr int clamp(int v, int lo, int hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

/// Divide toward negative infinity, matching Python's `//` so the C++, Python
/// and JS SDKs agree on negative inputs (both then clamp to the same floor).
constexpr int floor_div(int v, int d) noexcept {
    return (v < 0 && v % d != 0) ? (v / d - 1) : (v / d);
}

constexpr Frame one(std::uint8_t b) noexcept {
    Frame f;
    f.push_back(b);
    return f;
}

constexpr Frame two(std::uint8_t op, int arg) noexcept {
    Frame f;
    f.push_back(op);
    f.push_back(static_cast<std::uint8_t>(arg & 0xFF));
    return f;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Opacity — the legacy single-byte write
// ---------------------------------------------------------------------------

/// Legacy opacity write: ONE byte, 0 (clear) .. 255 (fully dark).
///
/// Intentionally a single byte — the firmware treats any 1-byte write as a
/// direct lens duty and stops whatever mode is running. This is the only frame
/// in the protocol that is allowed to be 1 byte long.
///
/// @param value opacity 0-255 (clamped)
constexpr Frame opacity(int value) noexcept {
    return detail::one(static_cast<std::uint8_t>(detail::clamp(value, 0, 255)));
}

/// Fully clear lens (opacity 0).
constexpr Frame clear_lens() noexcept { return opacity(0); }

/// Fully dark lens (opacity 255).
constexpr Frame dark_lens() noexcept { return opacity(255); }

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

/// Static mode at a fixed duty (0xA5) — the primary real-time dimming command.
/// On fw >= 4.16.2 this does NOT touch the 0xA2 brightness / breathe depth.
/// @param duty 0-100 % (clamped). Duty 1..100 maps to a perceptual floor on the
///        device (raw 265..1023); 0 is the only fully-clear value.
constexpr Frame set_static(int duty) noexcept {
    return detail::two(kStatic, detail::clamp(duty, 0, 100));
}

/// Persistent max-tint / breathe depth (0xA2). Persisted in NVS. Multiplies the
/// breathe / strobe / static output.
/// @param percent 0-100 % (clamped)
constexpr Frame set_brightness(int percent) noexcept {
    return detail::two(kBrightness, detail::clamp(percent, 0, 100));
}

/// Session duration (0xA4). Persisted in NVS; the device deep-sleeps at expiry.
/// The clock runs from device wake — writing this changes the total, not the origin.
/// @param minutes 1-60 (clamped)
constexpr Frame set_duration(int minutes) noexcept {
    return detail::two(kDuration, detail::clamp(minutes, 1, 60));
}

/// Strobe frequency (0xAB), integer-Hz form. Persisted in NVS.
/// @param hz 1-50 Hz (clamped)
constexpr Frame set_strobe_frequency(int hz) noexcept {
    return detail::two(kStrobeFrequency, detail::clamp(hz, 1, 50));
}

/// Strobe dark-phase duty (0xAC). Persisted in NVS.
/// @param percent 10-90 % (clamped)
constexpr Frame set_strobe_duty(int percent) noexcept {
    return detail::two(kStrobeDuty, detail::clamp(percent, 10, 90));
}

// ---------------------------------------------------------------------------
// Lens config (firmware >= 4.15.7; older firmware ignores these)
// ---------------------------------------------------------------------------

/// On-device lens smoothing (0xA0). Persisted in NVS.
///
/// The firmware glides between commanded static targets with an EMA of this
/// time constant, so the lens moves continuously between your writes instead of
/// stepping — the recommended way to get smooth real-time feedback. Send once
/// at connect. Rule of thumb: tau ~= 1-2x your write period (~80 ms is a good
/// general value). Affects commanded static duty only, never breathe/strobe.
///
/// Use fw >= 4.15.9 for a continuous stream: 4.15.7 stalls ~2-4 % short of each
/// target (fixed in 4.15.8), and through 4.15.8 the smoothed output was still
/// floored to ~101 duty levels; 4.15.9 drives the lens at full 10-bit PWM.
///
/// @param ms time constant 0-2550 ms, 10 ms resolution. 0 = off (snap).
constexpr Frame set_lens_smoothing(int ms) noexcept {
    return detail::two(kLensSmoothing, detail::clamp(detail::floor_div(ms, 10), 0, 255));
}

/// Hard slew cap on commanded static transitions (0xA1). Persisted in NVS.
/// Applied after the 0xA0 glide — a safety envelope guaranteeing the lens
/// cannot snap even if a host streams garbage. 40 ~= full-scale in 250 ms.
/// Also the on-device knob for a fixed timed fade (protocol doc §4.6.8).
/// @param percent_per_100ms 0-100 %/100 ms (clamped). 0 = unlimited (default).
constexpr Frame set_lens_max_rate(int percent_per_100ms) noexcept {
    return detail::two(kLensMaxRate, detail::clamp(percent_per_100ms, 0, 100));
}

/// What the lens does when the BLE link drops (0xA3). Persisted in NVS.
/// Factory default (false) FREEZES the lens at its last commanded output.
/// With true, the glasses stop any strobe and drop to a clear lens on link loss.
/// Fires when the firmware declares the link dead — bounded by the 32 s
/// supervision timeout, so still send an explicit clear before an intentional
/// disconnect.
constexpr Frame set_disconnect_behavior(bool fail_clear) noexcept {
    return detail::two(kDisconnectBehavior, fail_clear ? 0x01 : 0x00);
}

// ---------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------

/// Enter strobe mode (0xA6). Padded to the 2-byte minimum.
constexpr Frame start_strobe() noexcept { return detail::two(kStartStrobe, 0x00); }

/// Breathe rate (0xB1), integer BPM. Persisted. Use sync_breath() for fractional rates.
/// @param bpm 1-30 (clamped)
constexpr Frame set_breathe_bpm(int bpm) noexcept {
    return detail::two(kBreatheBpm, detail::clamp(bpm, 1, 30));
}

/// Inhale portion of the breath cycle (0xB2). Persisted.
/// @param percent 10-90 % (clamped)
constexpr Frame set_breathe_inhale_pct(int percent) noexcept {
    return detail::two(kBreatheInhalePct, detail::clamp(percent, 10, 90));
}

/// Hold at full-dark (0xB3). Persisted. Sent in 100 ms units.
/// @param ms 0-5000 ms (clamped, 100 ms resolution)
constexpr Frame set_breathe_hold_top(int ms) noexcept {
    return detail::two(kBreatheHoldTop, detail::clamp(detail::floor_div(ms, 100), 0, 50));
}

/// Hold at clear (0xB4). Persisted. Sent in 100 ms units.
/// @param ms 0-5000 ms (clamped, 100 ms resolution)
constexpr Frame set_breathe_hold_bottom(int ms) noexcept {
    return detail::two(kBreatheHoldBottom, detail::clamp(detail::floor_div(ms, 100), 0, 50));
}

/// Breathe waveform shape (0xB5). Persisted.
constexpr Frame set_breathe_waveform(Waveform w) noexcept {
    return detail::two(kBreatheWaveform, w == Waveform::Linear ? 1 : 0);
}

/// Enter breathe mode (0xB0). @param with_strobe true = breathe+strobe (fw >= 4.15.6).
constexpr Frame start_breathe(bool with_strobe) noexcept {
    return detail::two(kStartBreathe, with_strobe ? 0x01 : 0x00);
}

/// Phase-lock the breathe engine to an app-paced cycle (0xBA, fw >= 4.15.5).
///
/// Wire format: [0xBA, cycle_lo, cycle_hi, inhale_pct] — cycle length as u16 LE.
/// Restarts the breathe cosine at the instant of the write and sets the EXACT
/// cycle length in ms, which is how you get fractional breathing rates.
///
/// Send ONLY at the breath-cycle boundary (start of inhale), never mid-breath.
/// The override auto-expires 2 cycles after the last write, so re-send once per
/// breath to hold a fractional rate. Ignored by fw < 4.15.5, so always safe.
///
/// @param cycle_ms full breath cycle length in ms (clamped to u16; the firmware
///        additionally clamps to its own 2000-30000 ms window)
/// @param inhale_pct inhale portion 10-90 % (clamped)
constexpr Frame sync_breath(int cycle_ms, int inhale_pct) noexcept {
    const int c = detail::clamp(cycle_ms, 0, 65535);
    Frame f;
    f.push_back(kSyncBreath);
    f.push_back(static_cast<std::uint8_t>(c & 0xFF));
    f.push_back(static_cast<std::uint8_t>((c >> 8) & 0xFF));
    f.push_back(static_cast<std::uint8_t>(detail::clamp(inhale_pct, 10, 90)));
    return f;
}

// ---------------------------------------------------------------------------
// Power / maintenance
// ---------------------------------------------------------------------------

/// Enter deep sleep now (0xA7). Padded to the 2-byte minimum.
constexpr Frame sleep_now() noexcept { return detail::two(kSleep, 0x00); }

/// Reset all NVS-persisted settings to factory defaults (0xBF).
constexpr Frame factory_reset() noexcept { return detail::two(kFactoryReset, 0x00); }

/// Battery poll (0xC7, fw >= 4.16.1 on V1.2+ hardware). Triggers a 0xFB status
/// frame on 0xFF03 and refreshes 0x2A19.
/// @param reprobe false = refresh, true = re-probe the ADC channel
constexpr Frame battery_poll(bool reprobe) noexcept {
    return detail::two(kBatteryPoll, reprobe ? 0x01 : 0x00);
}

// ---------------------------------------------------------------------------
// Escape hatch
// ---------------------------------------------------------------------------

/// Build an arbitrary opcode frame, enforcing the >= 2-byte rule.
///
/// A 1-byte write is the legacy opacity command, so an argument-less opcode is
/// padded to [opcode, 0x00]. Use this for opcodes the typed builders above do
/// not cover — e.g. the 3-byte deci-Hz strobe form [0xAB, dHz_lo, dHz_hi] for
/// sub-Hz entrainment targets (protocol doc §4.6.6).
///
/// The firmware never NACKs: invalid opcodes and arguments are silently dropped
/// or clamped on the device, so validate values yourself.
///
/// @param opcode command opcode (low byte used)
/// @param payload optional argument bytes (may be null when n == 0)
/// @param n payload length; truncated to keep the frame within kMaxFrameSize
inline Frame command(int opcode, const std::uint8_t* payload, std::size_t n) noexcept {
    Frame f;
    f.push_back(static_cast<std::uint8_t>(opcode & 0xFF));
    if (payload != nullptr) {
        for (std::size_t i = 0; i < n; ++i) f.push_back(payload[i]);
    }
    if (f.size() < 2) f.push_back(0x00);  // never write 1 byte: that is opacity
    return f;
}

/// Argument-less overload: yields [opcode, 0x00].
inline Frame command(int opcode) noexcept { return command(opcode, nullptr, 0); }

}  // namespace protocol
}  // namespace edge

#endif  // EDGE_PROTOCOL_HPP
