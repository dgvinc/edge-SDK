// SPDX-License-Identifier: MIT
//
// Flat C ABI over the C++ SDK. Nothing here propagates a C++ exception across
// the boundary: every entry point catches, maps to an edge_status, and records
// a message retrievable with edge_last_error().

#include "edge/edge_glasses.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "edge/feedback_stream.hpp"
#include "edge/glasses.hpp"
#include "edge/protocol.hpp"
#include "edge/transport.hpp"

#if defined(EDGE_WITH_WINRT)
#include "edge/winrt_transport.hpp"
#endif

namespace {

constexpr const char* kVersion = "2.5.0";

/// Per-thread message for the last failure, so concurrent callers do not
/// clobber each other's diagnostics.
thread_local std::string g_last_error;

void set_error(const std::string& message) { g_last_error = message; }

void clear_error() { g_last_error.clear(); }

/// Map a C++ exception to an edge_status and record its message.
edge_status translate(const std::exception& e) {
    set_error(e.what());
    if (dynamic_cast<const edge::DeviceNotFoundError*>(&e)) return EDGE_ERR_DEVICE_NOT_FOUND;
    if (dynamic_cast<const edge::TimeoutError*>(&e)) return EDGE_ERR_TIMEOUT;
    if (dynamic_cast<const edge::ConnectionError*>(&e)) return EDGE_ERR_NOT_CONNECTED;
    if (dynamic_cast<const edge::CommandError*>(&e)) return EDGE_ERR_COMMAND;
    if (dynamic_cast<const edge::GlassesError*>(&e)) return EDGE_ERR_INTERNAL;
    return EDGE_ERR_INTERNAL;
}

/// Transport that forwards to C function pointers.
class CTransport : public edge::ITransport {
public:
    CTransport(edge_write_cb write_fn, edge_connected_cb connected_fn, void* user)
        : write_(write_fn), connected_(connected_fn), user_(user) {}

    bool is_connected() const override { return connected_(user_) != 0; }

    void write(const std::uint8_t* data, std::size_t len, bool with_response) override {
        const int rc = write_(user_, data, len, with_response ? 1 : 0);
        if (rc != 0) {
            throw edge::CommandError("write callback failed with code " + std::to_string(rc));
        }
    }

    bool supports_write_without_response() const override { return fast_write_; }

    bool read_battery_level(int& out_level) override {
        if (battery_ == nullptr) return false;
        const int rc = battery_(user_, &out_level);
        if (rc < 0) throw edge::CommandError("battery callback failed with code " + std::to_string(rc));
        return rc > 0;
    }

    void set_fast_write(bool v) { fast_write_ = v; }
    void set_battery_callback(edge_battery_cb fn) { battery_ = fn; }

private:
    edge_write_cb write_;
    edge_connected_cb connected_;
    void* user_;
    edge_battery_cb battery_ = nullptr;
    bool fast_write_ = false;
};

}  // namespace

struct edge_stream_s;

/// The opaque handle: a Glasses plus whichever transport backs it.
struct edge_glasses_s {
    std::unique_ptr<edge::ITransport> transport;
    std::unique_ptr<edge::Glasses> glasses;
    std::unique_ptr<edge::FeedbackStream> stream;
    std::unique_ptr<edge_stream_s> stream_handle;
    CTransport* c_transport = nullptr;  // non-owning; set for callback-backed handles
};

/// A stream handle is just a back-pointer to its controller, which owns the
/// single FeedbackStream. Keeping it a distinct type stops callers from passing
/// an edge_glasses where an edge_stream is expected.
struct edge_stream_s {
    edge_glasses_s* owner = nullptr;
};

namespace {

edge_glasses_s* owner_of(edge_stream stream) {
    return stream == nullptr ? nullptr : stream->owner;
}

int write_frame(std::uint8_t* out, std::size_t cap, const edge::protocol::Frame& f) {
    if (out == nullptr) {
        set_error("output buffer is NULL");
        return EDGE_ERR_INVALID_ARG;
    }
    if (cap < f.size()) {
        set_error("output buffer too small: need " + std::to_string(f.size()) + " bytes, got " +
                  std::to_string(cap));
        return EDGE_ERR_INVALID_ARG;
    }
    std::memcpy(out, f.data(), f.size());
    return static_cast<int>(f.size());
}

/// Run a Glasses call, converting any exception to a status code so nothing
/// unwinds across the C boundary.
template <typename Fn>
edge_status invoke(edge_glasses handle, Fn fn) {
    clear_error();
    if (handle == nullptr || handle->glasses == nullptr) {
        set_error("handle is NULL");
        return EDGE_ERR_INVALID_ARG;
    }
    try {
        fn(*handle->glasses);
        return EDGE_OK;
    } catch (const std::exception& e) {
        return translate(e);
    } catch (...) {
        set_error("unknown error");
        return EDGE_ERR_INTERNAL;
    }
}

edge::FeedbackStream* stream_of(edge_stream stream) {
    edge_glasses_s* owner = owner_of(stream);
    if (owner == nullptr) return nullptr;
    return owner->stream.get();
}

}  // namespace

extern "C" {

// ---------------------------------------------------------------------------
// Errors and version
// ---------------------------------------------------------------------------

const char* edge_last_error(void) { return g_last_error.c_str(); }

const char* edge_version(void) { return kVersion; }

const char* edge_status_name(edge_status status) {
    switch (status) {
        case EDGE_OK: return "EDGE_OK";
        case EDGE_ERR_INVALID_ARG: return "EDGE_ERR_INVALID_ARG";
        case EDGE_ERR_NOT_CONNECTED: return "EDGE_ERR_NOT_CONNECTED";
        case EDGE_ERR_DEVICE_NOT_FOUND: return "EDGE_ERR_DEVICE_NOT_FOUND";
        case EDGE_ERR_CONNECTION: return "EDGE_ERR_CONNECTION";
        case EDGE_ERR_COMMAND: return "EDGE_ERR_COMMAND";
        case EDGE_ERR_TIMEOUT: return "EDGE_ERR_TIMEOUT";
        case EDGE_ERR_UNSUPPORTED: return "EDGE_ERR_UNSUPPORTED";
        case EDGE_ERR_INTERNAL: return "EDGE_ERR_INTERNAL";
    }
    return "EDGE_ERR_UNKNOWN";
}

// ---------------------------------------------------------------------------
// Tier 1 — frame builders
// ---------------------------------------------------------------------------

int edge_frame_opacity(uint8_t* out, size_t cap, int value) {
    return write_frame(out, cap, edge::protocol::opacity(value));
}

int edge_frame_set_static(uint8_t* out, size_t cap, int duty) {
    return write_frame(out, cap, edge::protocol::set_static(duty));
}

int edge_frame_set_brightness(uint8_t* out, size_t cap, int percent) {
    return write_frame(out, cap, edge::protocol::set_brightness(percent));
}

int edge_frame_set_duration(uint8_t* out, size_t cap, int minutes) {
    return write_frame(out, cap, edge::protocol::set_duration(minutes));
}

int edge_frame_set_strobe_frequency(uint8_t* out, size_t cap, int hz) {
    return write_frame(out, cap, edge::protocol::set_strobe_frequency(hz));
}

int edge_frame_set_strobe_duty(uint8_t* out, size_t cap, int percent) {
    return write_frame(out, cap, edge::protocol::set_strobe_duty(percent));
}

int edge_frame_set_lens_smoothing(uint8_t* out, size_t cap, int ms) {
    return write_frame(out, cap, edge::protocol::set_lens_smoothing(ms));
}

int edge_frame_set_lens_max_rate(uint8_t* out, size_t cap, int percent_per_100ms) {
    return write_frame(out, cap, edge::protocol::set_lens_max_rate(percent_per_100ms));
}

int edge_frame_set_disconnect_behavior(uint8_t* out, size_t cap, int fail_clear) {
    return write_frame(out, cap, edge::protocol::set_disconnect_behavior(fail_clear != 0));
}

int edge_frame_start_strobe(uint8_t* out, size_t cap) {
    return write_frame(out, cap, edge::protocol::start_strobe());
}

int edge_frame_set_breathe_bpm(uint8_t* out, size_t cap, int bpm) {
    return write_frame(out, cap, edge::protocol::set_breathe_bpm(bpm));
}

int edge_frame_set_breathe_inhale_pct(uint8_t* out, size_t cap, int percent) {
    return write_frame(out, cap, edge::protocol::set_breathe_inhale_pct(percent));
}

int edge_frame_set_breathe_hold_top(uint8_t* out, size_t cap, int ms) {
    return write_frame(out, cap, edge::protocol::set_breathe_hold_top(ms));
}

int edge_frame_set_breathe_hold_bottom(uint8_t* out, size_t cap, int ms) {
    return write_frame(out, cap, edge::protocol::set_breathe_hold_bottom(ms));
}

int edge_frame_set_breathe_waveform(uint8_t* out, size_t cap, edge_waveform waveform) {
    const edge::Waveform w =
        waveform == EDGE_WAVEFORM_LINEAR ? edge::Waveform::Linear : edge::Waveform::Sine;
    return write_frame(out, cap, edge::protocol::set_breathe_waveform(w));
}

int edge_frame_start_breathe(uint8_t* out, size_t cap, int with_strobe) {
    return write_frame(out, cap, edge::protocol::start_breathe(with_strobe != 0));
}

int edge_frame_sync_breath(uint8_t* out, size_t cap, int cycle_ms, int inhale_pct) {
    return write_frame(out, cap, edge::protocol::sync_breath(cycle_ms, inhale_pct));
}

int edge_frame_sleep(uint8_t* out, size_t cap) {
    return write_frame(out, cap, edge::protocol::sleep_now());
}

int edge_frame_factory_reset(uint8_t* out, size_t cap) {
    return write_frame(out, cap, edge::protocol::factory_reset());
}

int edge_frame_battery_poll(uint8_t* out, size_t cap, int reprobe) {
    return write_frame(out, cap, edge::protocol::battery_poll(reprobe != 0));
}

int edge_frame_command(uint8_t* out, size_t cap, int opcode, const uint8_t* payload,
                       size_t payload_len) {
    return write_frame(out, cap, edge::protocol::command(opcode, payload, payload_len));
}

// ---------------------------------------------------------------------------
// Tier 2 — controller lifecycle
// ---------------------------------------------------------------------------

edge_status edge_glasses_create_with_callbacks(edge_write_cb write_fn,
                                               edge_connected_cb connected_fn, void* user,
                                               edge_glasses* out_handle) {
    clear_error();
    if (out_handle == nullptr || write_fn == nullptr || connected_fn == nullptr) {
        set_error("write_fn, connected_fn and out_handle must all be non-NULL");
        return EDGE_ERR_INVALID_ARG;
    }
    try {
        auto handle = std::unique_ptr<edge_glasses_s>(new edge_glasses_s());
        auto transport = std::unique_ptr<CTransport>(new CTransport(write_fn, connected_fn, user));
        handle->c_transport = transport.get();
        handle->transport = std::move(transport);
        handle->glasses.reset(new edge::Glasses(handle->transport.get()));
        *out_handle = handle.release();
        return EDGE_OK;
    } catch (const std::exception& e) {
        return translate(e);
    }
}

edge_status edge_glasses_create_winrt(edge_glasses* out_handle) {
    clear_error();
    if (out_handle == nullptr) {
        set_error("out_handle must be non-NULL");
        return EDGE_ERR_INVALID_ARG;
    }
#if defined(EDGE_WITH_WINRT)
    try {
        auto handle = std::unique_ptr<edge_glasses_s>(new edge_glasses_s());
        handle->transport.reset(new edge::WinRtTransport());
        handle->glasses.reset(new edge::Glasses(handle->transport.get()));
        *out_handle = handle.release();
        return EDGE_OK;
    } catch (const std::exception& e) {
        return translate(e);
    }
#else
    set_error(
        "this build has no WinRT transport (configure with -DEDGE_WITH_WINRT=ON on Windows, "
        "or use edge_glasses_create_with_callbacks)");
    return EDGE_ERR_UNSUPPORTED;
#endif
}

edge_status edge_glasses_set_fast_write(edge_glasses handle, int supported) {
    clear_error();
    if (handle == nullptr) return EDGE_ERR_INVALID_ARG;
    if (handle->c_transport == nullptr) {
        set_error("fast-write is detected automatically on built-in transports");
        return EDGE_ERR_UNSUPPORTED;
    }
    handle->c_transport->set_fast_write(supported != 0);
    return EDGE_OK;
}

edge_status edge_glasses_set_battery_callback(edge_glasses handle, edge_battery_cb fn) {
    clear_error();
    if (handle == nullptr) return EDGE_ERR_INVALID_ARG;
    if (handle->c_transport == nullptr) {
        set_error("battery is read directly on built-in transports");
        return EDGE_ERR_UNSUPPORTED;
    }
    handle->c_transport->set_battery_callback(fn);
    return EDGE_OK;
}

void edge_glasses_destroy(edge_glasses handle) {
    if (handle == nullptr) return;
    // Order matters: the stream's writer thread must be joined before the
    // Glasses and transport it writes through are torn down.
    handle->stream.reset();
    handle->stream_handle.reset();
    handle->glasses.reset();
    handle->transport.reset();
    delete handle;
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

edge_status edge_glasses_connect(edge_glasses handle, int timeout_ms) {
    return invoke(handle, [&](edge::Glasses& g) { g.connect(timeout_ms); });
}

edge_status edge_glasses_disconnect(edge_glasses handle) {
    return invoke(handle, [](edge::Glasses& g) { g.disconnect(); });
}

int edge_glasses_is_connected(edge_glasses handle) {
    if (handle == nullptr || handle->glasses == nullptr) return 0;
    try {
        return handle->glasses->is_connected() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int edge_glasses_supports_fast_write(edge_glasses handle) {
    if (handle == nullptr || handle->glasses == nullptr) return 0;
    try {
        return handle->glasses->supports_fast_write() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

edge_status edge_glasses_set_opacity(edge_glasses handle, int value) {
    return invoke(handle, [&](edge::Glasses& g) { g.set_opacity(value); });
}

edge_status edge_glasses_clear(edge_glasses handle) {
    return invoke(handle, [](edge::Glasses& g) { g.clear(); });
}

edge_status edge_glasses_dark(edge_glasses handle) {
    return invoke(handle, [](edge::Glasses& g) { g.dark(); });
}

edge_status edge_glasses_set_static(edge_glasses handle, int duty) {
    return invoke(handle, [&](edge::Glasses& g) { g.set_static(duty); });
}

edge_status edge_glasses_set_brightness(edge_glasses handle, int percent) {
    return invoke(handle, [&](edge::Glasses& g) { g.set_brightness(percent); });
}

edge_status edge_glasses_set_duration(edge_glasses handle, int minutes) {
    return invoke(handle, [&](edge::Glasses& g) { g.set_duration(minutes); });
}

edge_status edge_glasses_set_strobe_frequency(edge_glasses handle, int hz) {
    return invoke(handle, [&](edge::Glasses& g) { g.set_strobe_frequency(hz); });
}

edge_status edge_glasses_set_strobe_duty(edge_glasses handle, int percent) {
    return invoke(handle, [&](edge::Glasses& g) { g.set_strobe_duty(percent); });
}

edge_status edge_glasses_set_lens_smoothing(edge_glasses handle, int ms) {
    return invoke(handle, [&](edge::Glasses& g) { g.set_lens_smoothing(ms); });
}

edge_status edge_glasses_set_lens_max_rate(edge_glasses handle, int percent_per_100ms) {
    return invoke(handle, [&](edge::Glasses& g) { g.set_lens_max_rate(percent_per_100ms); });
}

edge_status edge_glasses_set_disconnect_behavior(edge_glasses handle, int fail_clear) {
    return invoke(handle, [&](edge::Glasses& g) { g.set_disconnect_behavior(fail_clear != 0); });
}

edge_status edge_glasses_start_strobe(edge_glasses handle, int hz, int duty_pct) {
    return invoke(handle, [&](edge::Glasses& g) {
        g.start_strobe(hz < 0 ? std::nullopt : std::optional<int>(hz),
                       duty_pct < 0 ? std::nullopt : std::optional<int>(duty_pct));
    });
}

edge_status edge_glasses_start_breathe(edge_glasses handle, int bpm, int inhale_pct,
                                       int hold_top_ms, int hold_bottom_ms, int waveform,
                                       int with_strobe) {
    return invoke(handle, [&](edge::Glasses& g) {
        edge::BreatheOptions o;
        if (bpm >= 0) o.bpm = bpm;
        if (inhale_pct >= 0) o.inhale_pct = inhale_pct;
        if (hold_top_ms >= 0) o.hold_top_ms = hold_top_ms;
        if (hold_bottom_ms >= 0) o.hold_bottom_ms = hold_bottom_ms;
        if (waveform >= 0) {
            o.waveform = waveform == EDGE_WAVEFORM_LINEAR ? edge::Waveform::Linear
                                                          : edge::Waveform::Sine;
        }
        o.with_strobe = with_strobe != 0;
        g.start_breathe(o);
    });
}

edge_status edge_glasses_sync_breath(edge_glasses handle, int cycle_ms, int inhale_pct) {
    return invoke(handle, [&](edge::Glasses& g) { g.sync_breath(cycle_ms, inhale_pct); });
}

edge_status edge_glasses_sleep(edge_glasses handle) {
    return invoke(handle, [](edge::Glasses& g) { g.sleep(); });
}

edge_status edge_glasses_factory_reset(edge_glasses handle) {
    return invoke(handle, [](edge::Glasses& g) { g.factory_reset(); });
}

edge_status edge_glasses_get_battery(edge_glasses handle, int* out_level) {
    clear_error();
    if (out_level == nullptr) {
        set_error("out_level must be non-NULL");
        return EDGE_ERR_INVALID_ARG;
    }
    bool present = false;
    int level = 0;
    const edge_status status = invoke(handle, [&](edge::Glasses& g) {
        const std::optional<int> value = g.get_battery();
        present = value.has_value();
        if (present) level = *value;
    });
    if (status != EDGE_OK) return status;
    if (!present) {
        set_error("this unit exposes no 0x180F Battery Service");
        return EDGE_ERR_UNSUPPORTED;
    }
    *out_level = level;
    return EDGE_OK;
}

edge_status edge_glasses_send_command(edge_glasses handle, int opcode, const uint8_t* payload,
                                      size_t payload_len) {
    return invoke(handle, [&](edge::Glasses& g) {
        std::vector<std::uint8_t> bytes;
        if (payload != nullptr && payload_len > 0) bytes.assign(payload, payload + payload_len);
        g.send_command(opcode, bytes);
    });
}

edge_status edge_glasses_session_relax(edge_glasses handle, int minutes) {
    return invoke(handle, [&](edge::Glasses& g) { g.session_relax(minutes); });
}

edge_status edge_glasses_session_meditate(edge_glasses handle, int minutes) {
    return invoke(handle, [&](edge::Glasses& g) { g.session_meditate(minutes); });
}

edge_status edge_glasses_session_focus(edge_glasses handle, int minutes) {
    return invoke(handle, [&](edge::Glasses& g) { g.session_focus(minutes); });
}

edge_status edge_glasses_session_sleep(edge_glasses handle, int minutes) {
    return invoke(handle, [&](edge::Glasses& g) { g.session_sleep(minutes); });
}

// ---------------------------------------------------------------------------
// Feedback stream
// ---------------------------------------------------------------------------

edge_status edge_glasses_start_feedback_stream(edge_glasses handle, double rate_hz,
                                               edge_stream* out_stream) {
    clear_error();
    if (handle == nullptr || handle->glasses == nullptr || out_stream == nullptr) {
        set_error("handle and out_stream must be non-NULL");
        return EDGE_ERR_INVALID_ARG;
    }
    try {
        // At most one stream per controller: replacing stops the previous
        // writer thread (without clearing, so the lens keeps its tint).
        if (handle->stream) handle->stream->stop(false);
        handle->stream = handle->glasses->start_feedback_stream(rate_hz);
        if (!handle->stream_handle) {
            handle->stream_handle.reset(new edge_stream_s());
            handle->stream_handle->owner = handle;
        }
        *out_stream = handle->stream_handle.get();
        return EDGE_OK;
    } catch (const std::exception& e) {
        return translate(e);
    }
}

edge_status edge_stream_feed(edge_stream stream, int duty) {
    clear_error();
    edge::FeedbackStream* s = stream_of(stream);
    if (s == nullptr) {
        set_error("stream handle is NULL or already stopped");
        return EDGE_ERR_INVALID_ARG;
    }
    s->feed(duty);
    return EDGE_OK;
}

edge_status edge_stream_feed_reward(edge_stream stream, double value) {
    clear_error();
    edge::FeedbackStream* s = stream_of(stream);
    if (s == nullptr) {
        set_error("stream handle is NULL or already stopped");
        return EDGE_ERR_INVALID_ARG;
    }
    s->feed_reward(value);
    return EDGE_OK;
}

edge_status edge_stream_reward_event(edge_stream stream, int duty, int hold_ms) {
    clear_error();
    edge::FeedbackStream* s = stream_of(stream);
    if (s == nullptr) {
        set_error("stream handle is NULL or already stopped");
        return EDGE_ERR_INVALID_ARG;
    }
    try {
        s->reward_event(duty, hold_ms);
        return EDGE_OK;
    } catch (const std::exception& e) {
        return translate(e);
    }
}

edge_status edge_stream_stop(edge_stream stream, int clear_lens) {
    clear_error();
    edge_glasses_s* owner = owner_of(stream);
    if (owner == nullptr || !owner->stream) {
        set_error("stream handle is NULL or already stopped");
        return EDGE_ERR_INVALID_ARG;
    }
    try {
        owner->stream->stop(clear_lens != 0);
        owner->stream.reset();
        return EDGE_OK;
    } catch (const std::exception& e) {
        owner->stream.reset();
        return translate(e);
    }
}

}  // extern "C"
