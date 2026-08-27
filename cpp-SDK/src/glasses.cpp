// SPDX-License-Identifier: MIT

#include "edge/glasses.hpp"

#include <utility>

#include "edge/feedback_stream.hpp"

namespace edge {

Glasses::Glasses(ITransport* transport) : transport_(transport) {
    if (transport_ == nullptr) throw ConnectionError("Glasses requires a transport (got null)");
}

Glasses::Glasses(std::unique_ptr<ITransport> transport)
    : transport_(transport.get()), owned_transport_(std::move(transport)) {
    if (transport_ == nullptr) throw ConnectionError("Glasses requires a transport (got null)");
}

Glasses::~Glasses() = default;

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void Glasses::connect(int timeout_ms) { transport_->connect(timeout_ms); }

void Glasses::disconnect() { transport_->disconnect(); }

bool Glasses::is_connected() const { return transport_->is_connected(); }

bool Glasses::supports_fast_write() const {
    return transport_->is_connected() && transport_->supports_write_without_response();
}

// ---------------------------------------------------------------------------
// Low-level
// ---------------------------------------------------------------------------

void Glasses::write_raw(const std::uint8_t* data, std::size_t len) {
    if (!transport_->is_connected()) {
        throw ConnectionError("Not connected. Call connect() first.");
    }
    // One write on the wire at a time: concurrent GATT writes to the same
    // characteristic fail outright on WinRT (protocol doc §4.3).
    std::lock_guard<std::mutex> lock(write_mutex_);
    transport_->write(data, len, /*with_response=*/true);
}

void Glasses::send_frame(const protocol::Frame& frame) {
    write_raw(frame.data(), frame.size());
}

void Glasses::send_command(int opcode, const std::vector<std::uint8_t>& payload) {
    send_frame(protocol::command(opcode, payload.empty() ? nullptr : payload.data(),
                                 payload.size()));
}

void Glasses::stream_static(int duty) {
    if (!transport_->is_connected()) {
        throw ConnectionError("Not connected. Call connect() first.");
    }
    const protocol::Frame frame = protocol::set_static(duty);
    // Write-without-response on firmware >= 4.16.3 skips the per-write ATT ack;
    // older firmware is with-response only, where the round-trip doubles as the
    // stream's rate limiter.
    const bool with_response = !transport_->supports_write_without_response();
    std::lock_guard<std::mutex> lock(write_mutex_);
    transport_->write(frame.data(), frame.size(), with_response);
}

// ---------------------------------------------------------------------------
// Opacity
// ---------------------------------------------------------------------------

void Glasses::set_opacity(int value) {
    // Deliberately a 1-byte write: the firmware reads any single byte as a
    // direct lens duty. send_frame() does not pad, so this stays 1 byte.
    send_frame(protocol::opacity(value));
}

void Glasses::clear() { set_opacity(0); }

void Glasses::dark() { set_opacity(255); }

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

void Glasses::set_static(int duty) { send_frame(protocol::set_static(duty)); }

void Glasses::set_brightness(int percent) { send_frame(protocol::set_brightness(percent)); }

void Glasses::set_duration(int minutes) { send_frame(protocol::set_duration(minutes)); }

void Glasses::set_strobe_frequency(int hz) { send_frame(protocol::set_strobe_frequency(hz)); }

void Glasses::set_strobe_duty(int percent) { send_frame(protocol::set_strobe_duty(percent)); }

// ---------------------------------------------------------------------------
// Lens config
// ---------------------------------------------------------------------------

void Glasses::set_lens_smoothing(int ms) { send_frame(protocol::set_lens_smoothing(ms)); }

void Glasses::set_lens_max_rate(int percent_per_100ms) {
    send_frame(protocol::set_lens_max_rate(percent_per_100ms));
}

void Glasses::set_disconnect_behavior(bool fail_clear) {
    send_frame(protocol::set_disconnect_behavior(fail_clear));
}

// ---------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------

void Glasses::start_strobe(std::optional<int> hz, std::optional<int> duty_pct) {
    if (hz.has_value()) set_strobe_frequency(*hz);
    if (duty_pct.has_value()) set_strobe_duty(*duty_pct);
    send_frame(protocol::start_strobe());
}

void Glasses::start_breathe(const BreatheOptions& options) {
    if (options.bpm.has_value()) send_frame(protocol::set_breathe_bpm(*options.bpm));
    if (options.inhale_pct.has_value()) {
        send_frame(protocol::set_breathe_inhale_pct(*options.inhale_pct));
    }
    if (options.hold_top_ms.has_value()) {
        send_frame(protocol::set_breathe_hold_top(*options.hold_top_ms));
    }
    if (options.hold_bottom_ms.has_value()) {
        send_frame(protocol::set_breathe_hold_bottom(*options.hold_bottom_ms));
    }
    if (options.waveform.has_value()) {
        send_frame(protocol::set_breathe_waveform(*options.waveform));
    }
    send_frame(protocol::start_breathe(options.with_strobe));
}

void Glasses::sync_breath(int cycle_ms, int inhale_pct) {
    send_frame(protocol::sync_breath(cycle_ms, inhale_pct));
}

// ---------------------------------------------------------------------------
// Power / maintenance
// ---------------------------------------------------------------------------

void Glasses::sleep() { send_frame(protocol::sleep_now()); }

void Glasses::factory_reset() { send_frame(protocol::factory_reset()); }

std::optional<int> Glasses::get_battery() {
    if (!transport_->is_connected()) {
        throw ConnectionError("Not connected. Call connect() first.");
    }
    int level = 0;
    if (!transport_->read_battery_level(level)) return std::nullopt;
    return protocol::detail::clamp(level, 0, 100);
}

// ---------------------------------------------------------------------------
// Preset sessions
// ---------------------------------------------------------------------------

void Glasses::session_relax(int duration_minutes) {
    set_brightness(100);
    BreatheOptions o;
    o.bpm = 5;
    o.waveform = Waveform::Sine;
    start_breathe(o);
    set_duration(duration_minutes);
}

void Glasses::session_meditate(int duration_minutes) {
    BreatheOptions o;
    o.bpm = 6;
    o.waveform = Waveform::Sine;
    start_breathe(o);
    set_duration(duration_minutes);
}

void Glasses::session_focus(int duration_minutes) {
    set_strobe_frequency(12);
    BreatheOptions o;
    o.bpm = 8;
    o.with_strobe = true;
    start_breathe(o);
    set_duration(duration_minutes);
}

void Glasses::session_sleep(int duration_minutes) {
    BreatheOptions o;
    o.bpm = 4;
    o.waveform = Waveform::Sine;
    start_breathe(o);
    set_duration(duration_minutes);
}

// ---------------------------------------------------------------------------
// Streaming
// ---------------------------------------------------------------------------

std::unique_ptr<FeedbackStream> Glasses::start_feedback_stream(double rate_hz) {
    return std::unique_ptr<FeedbackStream>(new FeedbackStream(*this, rate_hz));
}

}  // namespace edge
