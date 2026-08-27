// SPDX-License-Identifier: MIT
//
// EDGE Glasses C++ SDK — test suite.
//
// Zero external dependencies: build it with any C++17 compiler and run it.
// Every wire-format assertion is traceable to docs/bluetooth-protocol.md.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "edge/feedback_stream.hpp"
#include "edge/glasses.hpp"
#include "edge/protocol.hpp"
#include "mock_transport.hpp"

using edge::BreatheOptions;
using edge::Glasses;
using edge::Waveform;
using edge::test::MockTransport;
using edge::test::Write;
namespace proto = edge::protocol;

// ---------------------------------------------------------------------------
// Tiny test harness
// ---------------------------------------------------------------------------

static int g_checks = 0;
static int g_failures = 0;
static std::string g_current;

#define TEST(name)                     \
    g_current = name;                  \
    std::cout << "  " << name << "\n";

static std::string hex(const std::vector<std::uint8_t>& v) {
    std::ostringstream os;
    os << "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) os << ", ";
        os << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
           << static_cast<int>(v[i]);
    }
    os << "]";
    return os.str();
}

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::cout << "    FAIL [" << g_current << "] " << what << "\n";
    }
}

static void expect_bytes(const std::vector<std::uint8_t>& actual,
                         const std::vector<std::uint8_t>& expected, const std::string& what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::cout << "    FAIL [" << g_current << "] " << what << "\n"
                  << "         expected " << hex(expected) << "\n"
                  << "         actual   " << hex(actual) << "\n";
    }
}

static std::vector<std::uint8_t> bytes_of(const proto::Frame& f) {
    return std::vector<std::uint8_t>(f.begin(), f.end());
}

/// Emit one Glasses call and return the frames it produced.
static std::vector<Write> capture(MockTransport& t, const std::function<void()>& fn) {
    t.take_writes();
    fn();
    return t.take_writes();
}

// ---------------------------------------------------------------------------
// 1. Protocol encoding — exact wire bytes
// ---------------------------------------------------------------------------

static void test_protocol_frames() {
    std::cout << "\nprotocol: frame encoding\n";

    TEST("legacy opacity is a single byte");
    expect_bytes(bytes_of(proto::opacity(0)), {0x00}, "opacity(0)");
    expect_bytes(bytes_of(proto::opacity(128)), {0x80}, "opacity(128)");
    expect_bytes(bytes_of(proto::opacity(255)), {0xFF}, "opacity(255)");
    check(proto::opacity(128).is_legacy_opacity(), "opacity frame is flagged 1-byte");
    check(proto::opacity(166).size() == 1, "opacity is never padded to 2 bytes");
    expect_bytes(bytes_of(proto::clear_lens()), {0x00}, "clear_lens()");
    expect_bytes(bytes_of(proto::dark_lens()), {0xFF}, "dark_lens()");

    TEST("opacity clamps to 0-255");
    expect_bytes(bytes_of(proto::opacity(-1)), {0x00}, "opacity(-1)");
    expect_bytes(bytes_of(proto::opacity(999)), {0xFF}, "opacity(999)");

    TEST("static duty 0xA5");
    expect_bytes(bytes_of(proto::set_static(0)), {0xA5, 0}, "set_static(0)");
    expect_bytes(bytes_of(proto::set_static(50)), {0xA5, 50}, "set_static(50)");
    expect_bytes(bytes_of(proto::set_static(100)), {0xA5, 100}, "set_static(100)");
    expect_bytes(bytes_of(proto::set_static(-5)), {0xA5, 0}, "set_static clamps low");
    expect_bytes(bytes_of(proto::set_static(255)), {0xA5, 100}, "set_static clamps high");

    TEST("brightness 0xA2 clamps 0-100");
    expect_bytes(bytes_of(proto::set_brightness(80)), {0xA2, 80}, "set_brightness(80)");
    expect_bytes(bytes_of(proto::set_brightness(-1)), {0xA2, 0}, "clamps low");
    expect_bytes(bytes_of(proto::set_brightness(101)), {0xA2, 100}, "clamps high");

    TEST("duration 0xA4 clamps 1-60");
    expect_bytes(bytes_of(proto::set_duration(60)), {0xA4, 60}, "set_duration(60)");
    expect_bytes(bytes_of(proto::set_duration(0)), {0xA4, 1}, "clamps to min 1");
    expect_bytes(bytes_of(proto::set_duration(120)), {0xA4, 60}, "clamps to max 60");

    TEST("strobe frequency 0xAB clamps 1-50");
    expect_bytes(bytes_of(proto::set_strobe_frequency(10)), {0xAB, 10}, "10 Hz");
    expect_bytes(bytes_of(proto::set_strobe_frequency(0)), {0xAB, 1}, "clamps to 1");
    expect_bytes(bytes_of(proto::set_strobe_frequency(99)), {0xAB, 50}, "clamps to 50");

    TEST("strobe duty 0xAC clamps 10-90");
    expect_bytes(bytes_of(proto::set_strobe_duty(50)), {0xAC, 50}, "50 %");
    expect_bytes(bytes_of(proto::set_strobe_duty(0)), {0xAC, 10}, "clamps to 10");
    expect_bytes(bytes_of(proto::set_strobe_duty(100)), {0xAC, 90}, "clamps to 90");

    TEST("lens smoothing 0xA0 is tau in 10 ms units");
    expect_bytes(bytes_of(proto::set_lens_smoothing(0)), {0xA0, 0}, "0 ms = off");
    expect_bytes(bytes_of(proto::set_lens_smoothing(80)), {0xA0, 8}, "80 ms -> tau 8");
    expect_bytes(bytes_of(proto::set_lens_smoothing(100)), {0xA0, 10}, "100 ms -> tau 10");
    expect_bytes(bytes_of(proto::set_lens_smoothing(85)), {0xA0, 8}, "85 ms truncates to 8");
    expect_bytes(bytes_of(proto::set_lens_smoothing(2550)), {0xA0, 255}, "2550 ms -> tau 255");
    expect_bytes(bytes_of(proto::set_lens_smoothing(9999)), {0xA0, 255}, "clamps to 255");
    expect_bytes(bytes_of(proto::set_lens_smoothing(-50)), {0xA0, 0}, "negative clamps to 0");

    TEST("lens max rate 0xA1 clamps 0-100");
    expect_bytes(bytes_of(proto::set_lens_max_rate(5)), {0xA1, 5}, "5 %/100ms");
    expect_bytes(bytes_of(proto::set_lens_max_rate(-1)), {0xA1, 0}, "clamps low");
    expect_bytes(bytes_of(proto::set_lens_max_rate(200)), {0xA1, 100}, "clamps high");

    TEST("disconnect behavior 0xA3");
    expect_bytes(bytes_of(proto::set_disconnect_behavior(false)), {0xA3, 0x00}, "freeze");
    expect_bytes(bytes_of(proto::set_disconnect_behavior(true)), {0xA3, 0x01}, "fail clear");

    TEST("breathe parameter opcodes");
    expect_bytes(bytes_of(proto::set_breathe_bpm(6)), {0xB1, 6}, "6 BPM");
    expect_bytes(bytes_of(proto::set_breathe_bpm(0)), {0xB1, 1}, "clamps to 1");
    expect_bytes(bytes_of(proto::set_breathe_bpm(99)), {0xB1, 30}, "clamps to 30");
    expect_bytes(bytes_of(proto::set_breathe_inhale_pct(40)), {0xB2, 40}, "40 % inhale");
    expect_bytes(bytes_of(proto::set_breathe_inhale_pct(5)), {0xB2, 10}, "clamps to 10");
    expect_bytes(bytes_of(proto::set_breathe_inhale_pct(95)), {0xB2, 90}, "clamps to 90");
    expect_bytes(bytes_of(proto::set_breathe_hold_top(1000)), {0xB3, 10}, "1000 ms -> 10 units");
    expect_bytes(bytes_of(proto::set_breathe_hold_top(9999)), {0xB3, 50}, "clamps to 50 units");
    expect_bytes(bytes_of(proto::set_breathe_hold_bottom(500)), {0xB4, 5}, "500 ms -> 5 units");
    expect_bytes(bytes_of(proto::set_breathe_hold_bottom(-100)), {0xB4, 0}, "negative -> 0");
    expect_bytes(bytes_of(proto::set_breathe_waveform(Waveform::Sine)), {0xB5, 0}, "sine");
    expect_bytes(bytes_of(proto::set_breathe_waveform(Waveform::Linear)), {0xB5, 1}, "linear");

    TEST("mode entry opcodes are padded to 2 bytes");
    expect_bytes(bytes_of(proto::start_breathe(false)), {0xB0, 0x00}, "plain breathe");
    expect_bytes(bytes_of(proto::start_breathe(true)), {0xB0, 0x01}, "breathe+strobe");
    expect_bytes(bytes_of(proto::start_strobe()), {0xA6, 0x00}, "strobe");
    expect_bytes(bytes_of(proto::sleep_now()), {0xA7, 0x00}, "sleep");
    expect_bytes(bytes_of(proto::factory_reset()), {0xBF, 0x00}, "factory reset");
    expect_bytes(bytes_of(proto::battery_poll(false)), {0xC7, 0x00}, "battery refresh");
    expect_bytes(bytes_of(proto::battery_poll(true)), {0xC7, 0x01}, "battery reprobe");

    TEST("sync_breath packs cycle_ms as u16 little-endian");
    // 5500 ms = 0x157C -> lo 0x7C, hi 0x15
    expect_bytes(bytes_of(proto::sync_breath(5500, 40)), {0xBA, 0x7C, 0x15, 40}, "5500 ms");
    // 10000 ms = 0x2710 -> lo 0x10, hi 0x27
    expect_bytes(bytes_of(proto::sync_breath(10000, 50)), {0xBA, 0x10, 0x27, 50}, "10000 ms");
    expect_bytes(bytes_of(proto::sync_breath(255, 40)), {0xBA, 0xFF, 0x00, 40}, "low byte only");
    expect_bytes(bytes_of(proto::sync_breath(256, 40)), {0xBA, 0x00, 0x01, 40}, "high byte only");
    expect_bytes(bytes_of(proto::sync_breath(70000, 40)), {0xBA, 0xFF, 0xFF, 40}, "clamps to u16");
    expect_bytes(bytes_of(proto::sync_breath(-1, 40)), {0xBA, 0x00, 0x00, 40}, "clamps negative");
    expect_bytes(bytes_of(proto::sync_breath(5000, 5)), {0xBA, 0x88, 0x13, 10}, "inhale clamps low");
    expect_bytes(bytes_of(proto::sync_breath(5000, 99)), {0xBA, 0x88, 0x13, 90},
                 "inhale clamps high");
}

// ---------------------------------------------------------------------------
// 2. The >= 2-byte rule
// ---------------------------------------------------------------------------

static void test_two_byte_rule() {
    std::cout << "\nprotocol: the >= 2-byte rule\n";

    TEST("argument-less opcodes are padded with 0x00");
    expect_bytes(bytes_of(proto::command(0xA6)), {0xA6, 0x00}, "command(0xA6)");
    expect_bytes(bytes_of(proto::command(0xA7)), {0xA7, 0x00}, "command(0xA7)");
    expect_bytes(bytes_of(proto::command(0xB0)), {0xB0, 0x00}, "command(0xB0)");

    TEST("opcodes with payload are not padded");
    const std::uint8_t one[] = {80};
    expect_bytes(bytes_of(proto::command(0xA2, one, 1)), {0xA2, 80}, "command(0xA2, [80])");
    const std::uint8_t three[] = {0x7C, 0x15, 40};
    expect_bytes(bytes_of(proto::command(0xBA, three, 3)), {0xBA, 0x7C, 0x15, 40},
                 "command(0xBA, 3 bytes)");

    TEST("every generated frame except opacity is >= 2 bytes");
    const proto::Frame frames[] = {
        proto::set_static(50),          proto::set_brightness(50),
        proto::set_duration(30),        proto::set_strobe_frequency(10),
        proto::set_strobe_duty(50),     proto::set_lens_smoothing(80),
        proto::set_lens_max_rate(5),    proto::set_disconnect_behavior(true),
        proto::set_breathe_bpm(6),      proto::set_breathe_inhale_pct(40),
        proto::set_breathe_hold_top(0), proto::set_breathe_hold_bottom(0),
        proto::set_breathe_waveform(Waveform::Sine),
        proto::start_breathe(false),    proto::start_strobe(),
        proto::sleep_now(),             proto::factory_reset(),
        proto::sync_breath(5000, 40),   proto::battery_poll(false),
    };
    for (const auto& f : frames) {
        check(f.size() >= 2, "frame with opcode 0x" +
                                 [&] {
                                     std::ostringstream os;
                                     os << std::hex << std::uppercase << static_cast<int>(f[0]);
                                     return os.str();
                                 }() +
                                 " is at least 2 bytes");
    }

    TEST("command() truncates rather than overflowing its buffer");
    std::vector<std::uint8_t> huge(64, 0xAA);
    const proto::Frame f = proto::command(0xE0, huge.data(), huge.size());
    check(f.size() == proto::kMaxFrameSize, "oversized payload is truncated to kMaxFrameSize");
    check(f[0] == 0xE0, "opcode survives truncation");
}

// ---------------------------------------------------------------------------
// 3. Glasses — command sequences over a mock transport
// ---------------------------------------------------------------------------

static void test_glasses_commands() {
    std::cout << "\nglasses: command sequences\n";

    MockTransport t;
    Glasses g(&t);

    TEST("set_opacity writes exactly one byte, with response");
    auto w = capture(t, [&] { g.set_opacity(128); });
    check(w.size() == 1, "one write");
    expect_bytes(w[0].bytes, {0x80}, "single-byte opacity");
    check(w[0].with_response, "opacity uses write-with-response");

    TEST("clear / dark");
    w = capture(t, [&] { g.clear(); });
    expect_bytes(w[0].bytes, {0x00}, "clear()");
    w = capture(t, [&] { g.dark(); });
    expect_bytes(w[0].bytes, {0xFF}, "dark()");

    TEST("command writes use write-with-response");
    w = capture(t, [&] { g.set_static(50); });
    expect_bytes(w[0].bytes, {0xA5, 50}, "set_static(50)");
    check(w[0].with_response, "set_static uses write-with-response");

    TEST("send_command pads and forwards");
    w = capture(t, [&] { g.send_command(0xA7); });
    expect_bytes(w[0].bytes, {0xA7, 0x00}, "send_command(0xA7)");
    w = capture(t, [&] { g.send_command(0xA2, {80}); });
    expect_bytes(w[0].bytes, {0xA2, 80}, "send_command(0xA2, {80})");

    TEST("start_strobe writes only the parameters supplied");
    w = capture(t, [&] { g.start_strobe(); });
    check(w.size() == 1, "no parameters -> just the mode write");
    expect_bytes(w[0].bytes, {0xA6, 0x00}, "start_strobe()");

    w = capture(t, [&] { g.start_strobe(10, 50); });
    check(w.size() == 3, "hz + duty + mode = 3 writes");
    expect_bytes(w[0].bytes, {0xAB, 10}, "frequency first");
    expect_bytes(w[1].bytes, {0xAC, 50}, "duty second");
    expect_bytes(w[2].bytes, {0xA6, 0x00}, "mode last");

    w = capture(t, [&] { g.start_strobe(std::nullopt, 40); });
    check(w.size() == 2, "duty only -> 2 writes");
    expect_bytes(w[0].bytes, {0xAC, 40}, "duty");
    expect_bytes(w[1].bytes, {0xA6, 0x00}, "mode");

    TEST("start_breathe writes only the parameters supplied, mode last");
    w = capture(t, [&] { g.start_breathe(); });
    check(w.size() == 1, "no options -> just the mode write");
    expect_bytes(w[0].bytes, {0xB0, 0x00}, "start_breathe()");

    BreatheOptions o;
    o.bpm = 5;
    o.inhale_pct = 40;
    o.hold_top_ms = 1000;
    o.hold_bottom_ms = 500;
    o.waveform = Waveform::Sine;
    w = capture(t, [&] { g.start_breathe(o); });
    check(w.size() == 6, "5 params + mode = 6 writes");
    expect_bytes(w[0].bytes, {0xB1, 5}, "bpm");
    expect_bytes(w[1].bytes, {0xB2, 40}, "inhale %");
    expect_bytes(w[2].bytes, {0xB3, 10}, "hold top");
    expect_bytes(w[3].bytes, {0xB4, 5}, "hold bottom");
    expect_bytes(w[4].bytes, {0xB5, 0}, "waveform");
    expect_bytes(w[5].bytes, {0xB0, 0x00}, "mode last");

    BreatheOptions strobe_opts;
    strobe_opts.bpm = 8;
    strobe_opts.with_strobe = true;
    w = capture(t, [&] { g.start_breathe(strobe_opts); });
    expect_bytes(w[1].bytes, {0xB0, 0x01}, "breathe+strobe mode byte");

    TEST("sync_breath");
    w = capture(t, [&] { g.sync_breath(5500); });
    expect_bytes(w[0].bytes, {0xBA, 0x7C, 0x15, 40}, "default inhale 40 %");
    w = capture(t, [&] { g.sync_breath(5500, 50); });
    expect_bytes(w[0].bytes, {0xBA, 0x7C, 0x15, 50}, "explicit inhale");

    TEST("lens config");
    w = capture(t, [&] { g.set_lens_smoothing(80); });
    expect_bytes(w[0].bytes, {0xA0, 8}, "smoothing 80 ms");
    w = capture(t, [&] { g.set_lens_max_rate(5); });
    expect_bytes(w[0].bytes, {0xA1, 5}, "max rate");
    w = capture(t, [&] { g.set_disconnect_behavior(true); });
    expect_bytes(w[0].bytes, {0xA3, 0x01}, "fail clear");

    TEST("power / maintenance");
    w = capture(t, [&] { g.sleep(); });
    expect_bytes(w[0].bytes, {0xA7, 0x00}, "sleep()");
    w = capture(t, [&] { g.factory_reset(); });
    expect_bytes(w[0].bytes, {0xBF, 0x00}, "factory_reset()");
}

// ---------------------------------------------------------------------------
// 4. Preset sessions — order must match the Python SDK
// ---------------------------------------------------------------------------

static void test_presets() {
    std::cout << "\nglasses: preset sessions\n";

    MockTransport t;
    Glasses g(&t);

    // Write order follows the Python SDK (edge_glasses.Glasses): parameters,
    // then the mode entry, then the session duration.
    TEST("session_relax: brightness 100, 5 BPM sine, duration");
    auto w = capture(t, [&] { g.session_relax(10); });
    check(w.size() == 5, "5 writes");
    expect_bytes(w[0].bytes, {0xA2, 100}, "brightness 100");
    expect_bytes(w[1].bytes, {0xB1, 5}, "5 BPM");
    expect_bytes(w[2].bytes, {0xB5, 0}, "sine");
    expect_bytes(w[3].bytes, {0xB0, 0x00}, "enter breathe");
    expect_bytes(w[4].bytes, {0xA4, 10}, "duration last");

    TEST("session_meditate: 6 BPM sine + duration");
    w = capture(t, [&] { g.session_meditate(10); });
    check(w.size() == 4, "4 writes");
    expect_bytes(w[0].bytes, {0xB1, 6}, "6 BPM");
    expect_bytes(w[1].bytes, {0xB5, 0}, "sine");
    expect_bytes(w[2].bytes, {0xB0, 0x00}, "enter breathe");
    expect_bytes(w[3].bytes, {0xA4, 10}, "duration");

    TEST("session_focus: 12 Hz strobe, 8 BPM breathe+strobe");
    w = capture(t, [&] { g.session_focus(15); });
    check(w.size() == 4, "4 writes");
    expect_bytes(w[0].bytes, {0xAB, 12}, "strobe 12 Hz");
    expect_bytes(w[1].bytes, {0xB1, 8}, "8 BPM");
    expect_bytes(w[2].bytes, {0xB0, 0x01}, "breathe+strobe");
    expect_bytes(w[3].bytes, {0xA4, 15}, "duration");

    TEST("session_sleep: 4 BPM sine");
    w = capture(t, [&] { g.session_sleep(15); });
    check(w.size() == 4, "4 writes");
    expect_bytes(w[0].bytes, {0xB1, 4}, "4 BPM");
    expect_bytes(w[1].bytes, {0xB5, 0}, "sine");
    expect_bytes(w[2].bytes, {0xB0, 0x00}, "enter breathe");
    expect_bytes(w[3].bytes, {0xA4, 15}, "duration");
}

// ---------------------------------------------------------------------------
// 5. Connection state, errors, battery
// ---------------------------------------------------------------------------

static void test_connection_and_errors() {
    std::cout << "\nglasses: connection state and errors\n";

    MockTransport t;
    Glasses g(&t);

    TEST("commands on a disconnected link raise ConnectionError");
    t.set_connected(false);
    bool threw = false;
    try {
        g.set_static(50);
    } catch (const edge::ConnectionError&) {
        threw = true;
    }
    check(threw, "set_static throws ConnectionError");
    check(t.write_count() == 0, "nothing reached the transport");

    threw = false;
    try {
        g.set_opacity(10);
    } catch (const edge::ConnectionError&) {
        threw = true;
    }
    check(threw, "set_opacity throws ConnectionError");

    threw = false;
    try {
        g.stream_static(10);
    } catch (const edge::ConnectionError&) {
        threw = true;
    }
    check(threw, "stream_static throws ConnectionError");

    threw = false;
    try {
        g.get_battery();
    } catch (const edge::ConnectionError&) {
        threw = true;
    }
    check(threw, "get_battery throws ConnectionError");

    TEST("ConnectionError is a GlassesError");
    threw = false;
    try {
        g.set_static(50);
    } catch (const edge::GlassesError&) {
        threw = true;
    }
    check(threw, "caught as the base type");

    TEST("connect / disconnect forward to the transport");
    g.connect(5000);
    check(t.connect_calls == 1, "connect forwarded");
    check(g.is_connected(), "is_connected true after connect");
    g.disconnect();
    check(t.disconnect_calls == 1, "disconnect forwarded");
    check(!g.is_connected(), "is_connected false after disconnect");

    TEST("transport write failures surface as CommandError");
    t.set_connected(true);
    t.fail_next_writes(1);
    threw = false;
    try {
        g.set_static(50);
    } catch (const edge::CommandError&) {
        threw = true;
    }
    check(threw, "CommandError propagates");

    TEST("get_battery reports absence as nullopt");
    t.set_battery(false);
    check(!g.get_battery().has_value(), "no 0x180F -> nullopt");

    TEST("get_battery clamps the reported level to 0-100");
    t.set_battery(true, 77);
    check(g.get_battery().value_or(-1) == 77, "77 %");
    t.set_battery(true, 200);
    check(g.get_battery().value_or(-1) == 100, "clamps above 100");

    TEST("null transport is rejected");
    threw = false;
    try {
        Glasses bad(static_cast<edge::ITransport*>(nullptr));
    } catch (const edge::GlassesError&) {
        threw = true;
    }
    check(threw, "constructing with null throws");
}

// ---------------------------------------------------------------------------
// 6. The fast-write path (firmware >= 4.16.3)
// ---------------------------------------------------------------------------

static void test_fast_write() {
    std::cout << "\nglasses: write-without-response feature detection\n";

    MockTransport t;
    Glasses g(&t);

    TEST("without the property, streaming uses write-with-response");
    t.set_fast_write(false);
    check(!g.supports_fast_write(), "supports_fast_write false");
    auto w = capture(t, [&] { g.stream_static(42); });
    expect_bytes(w[0].bytes, {0xA5, 42}, "0xA5 frame");
    check(w[0].with_response, "with-response on pre-4.16.3 firmware");

    TEST("with the property, streaming uses write-without-response");
    t.set_fast_write(true);
    check(g.supports_fast_write(), "supports_fast_write true");
    w = capture(t, [&] { g.stream_static(42); });
    check(!w[0].with_response, "without-response on fw >= 4.16.3");

    TEST("command writes stay with-response even on fast-write firmware");
    w = capture(t, [&] { g.set_static(42); });
    check(w[0].with_response, "set_static keeps ordering/back-pressure");
    w = capture(t, [&] { g.set_duration(60); });
    check(w[0].with_response, "set_duration keeps ordering/back-pressure");

    TEST("stream_static clamps duty");
    w = capture(t, [&] { g.stream_static(-10); });
    expect_bytes(w[0].bytes, {0xA5, 0}, "clamps low");
    w = capture(t, [&] { g.stream_static(500); });
    expect_bytes(w[0].bytes, {0xA5, 100}, "clamps high");

    TEST("supports_fast_write is false while disconnected");
    t.set_connected(false);
    check(!g.supports_fast_write(), "false when the link is down");
}

// ---------------------------------------------------------------------------
// 7. FeedbackStream
// ---------------------------------------------------------------------------

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static void test_feedback_stream() {
    std::cout << "\nFeedbackStream\n";

    TEST("rate is clamped to 1-45 Hz");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(1000.0);
        check(s->rate_hz() == edge::FeedbackStream::kMaxRateHz, "1000 Hz clamps to 45");
        s->stop(false);
        auto s2 = g.start_feedback_stream(0.01);
        check(s2->rate_hz() == edge::FeedbackStream::kMinRateHz, "0.01 Hz clamps to 1");
        s2->stop(false);
        auto s3 = g.start_feedback_stream(30.0);
        check(s3->rate_hz() == 30.0, "30 Hz is kept");
        s3->stop(false);
    }

    TEST("no writes before the first feed()");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(45.0);
        sleep_ms(120);
        check(t.write_count() == 0, "silent until fed");
        s->stop(false);
    }

    TEST("feed() reaches the lens as a 0xA5 write");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(45.0);
        s->feed(60);
        sleep_ms(150);
        auto w = t.snapshot();
        check(!w.empty(), "at least one write");
        if (!w.empty()) expect_bytes(w[0].bytes, {0xA5, 60}, "duty 60");
        s->stop(false);
    }

    TEST("unchanged values are coalesced away");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(45.0);
        s->feed(30);
        sleep_ms(250);  // ~11 ticks at 45 Hz
        const std::size_t after_first = t.write_count();
        check(after_first == 1, "one write for a constant value");
        s->feed(30);
        sleep_ms(150);
        check(t.write_count() == after_first, "re-feeding the same value writes nothing");
        s->feed(31);
        sleep_ms(150);
        check(t.write_count() == after_first + 1, "a changed value writes once");
        s->stop(false);
    }

    TEST("feed_reward maps 0..1 to duty (1 = in condition = clear)");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(45.0);
        s->feed_reward(1.0);
        sleep_ms(120);
        auto w = t.take_writes();
        check(!w.empty(), "reward 1.0 wrote");
        if (!w.empty()) expect_bytes(w[0].bytes, {0xA5, 0}, "1.0 -> duty 0 (clear)");

        s->feed_reward(0.0);
        sleep_ms(120);
        w = t.take_writes();
        check(!w.empty(), "reward 0.0 wrote");
        if (!w.empty()) expect_bytes(w[0].bytes, {0xA5, 100}, "0.0 -> duty 100 (dark)");

        s->feed_reward(0.25);
        sleep_ms(120);
        w = t.take_writes();
        check(!w.empty(), "reward 0.25 wrote");
        if (!w.empty()) expect_bytes(w[0].bytes, {0xA5, 75}, "0.25 -> duty 75");
        s->stop(false);
    }

    TEST("feed / feed_reward clamp out-of-range input");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(45.0);
        s->feed(500);
        sleep_ms(120);
        auto w = t.take_writes();
        if (!w.empty()) expect_bytes(w[0].bytes, {0xA5, 100}, "feed(500) -> 100");
        s->feed(-500);
        sleep_ms(120);
        w = t.take_writes();
        if (!w.empty()) expect_bytes(w[0].bytes, {0xA5, 0}, "feed(-500) -> 0");
        s->feed_reward(9.0);
        sleep_ms(120);
        w = t.take_writes();
        check(w.empty(), "reward 9.0 clamps to 1.0 -> duty 0, already sent (coalesced)");
        s->stop(false);
    }

    TEST("reward_event writes immediately, without waiting for a tick");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(1.0);  // 1 Hz: a tick is a second away
        const auto t0 = std::chrono::steady_clock::now();
        s->reward_event(0, 0);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count();
        auto w = t.snapshot();
        check(!w.empty(), "reward wrote");
        if (!w.empty()) expect_bytes(w[0].bytes, {0xA5, 0}, "reward duty 0");
        check(elapsed < 200, "returned well inside the 1000 ms tick period");
        s->stop(false);
    }

    TEST("reward_event(duty) honours a non-zero reward tint");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(1.0);
        s->reward_event(20, 0);
        auto w = t.snapshot();
        check(!w.empty(), "wrote");
        if (!w.empty()) expect_bytes(w[0].bytes, {0xA5, 20}, "duty 20");
        s->stop(false);
    }

    TEST("hold_ms suppresses the proportional stream, then it resumes");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(45.0);
        s->reward_event(0, 250);  // hold clear for 250 ms
        t.take_writes();          // drop the reward write itself
        s->feed(90);              // the stream wants to darken immediately
        sleep_ms(120);
        check(t.write_count() == 0, "stream is held off during hold_ms");
        sleep_ms(250);
        auto w = t.snapshot();
        check(!w.empty(), "stream resumes after the hold expires");
        if (!w.empty()) expect_bytes(w.back().bytes, {0xA5, 90}, "resumed at the fed duty");
        s->stop(false);
    }

    TEST("a failed write is retried on the next tick");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(45.0);
        t.fail_next_writes(1);
        s->feed(55);
        sleep_ms(250);
        check(t.failed_writes == 1, "one write was failed by the mock");
        auto w = t.snapshot();
        check(!w.empty(), "the value was retried and landed");
        if (!w.empty()) expect_bytes(w[0].bytes, {0xA5, 55}, "retried duty 55");
        s->stop(false);
    }

    TEST("stop() clears the lens by default and is idempotent");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(45.0);
        s->feed(80);
        sleep_ms(120);
        t.take_writes();
        s->stop();  // clear_lens = true
        auto w = t.snapshot();
        check(w.size() == 1, "exactly one write on stop");
        if (!w.empty()) expect_bytes(w[0].bytes, {0x00}, "clear = 1-byte opacity 0");
        check(!s->is_running(), "writer stopped");
        s->stop();
        check(t.snapshot().size() == 1, "second stop() writes nothing");
    }

    TEST("stop(false) leaves the lens at its last tint");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(45.0);
        s->feed(80);
        sleep_ms(120);
        t.take_writes();
        s->stop(false);
        check(t.write_count() == 0, "no clear write");
    }

    TEST("stop() on a disconnected link does not throw");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(45.0);
        s->feed(80);
        sleep_ms(120);
        t.set_connected(false);
        s->stop(true);
        check(!s->is_running(), "stopped cleanly with the link down");
    }

    TEST("destructor stops the writer thread");
    {
        MockTransport t;
        Glasses g(&t);
        {
            auto s = g.start_feedback_stream(45.0);
            s->feed(40);
            sleep_ms(80);
        }  // ~FeedbackStream runs here
        const std::size_t n = t.write_count();
        sleep_ms(150);
        check(t.write_count() == n, "no writes after destruction");
    }

    TEST("feed() from many threads is safe");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(45.0);
        std::atomic<bool> go{true};
        std::vector<std::thread> workers;
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([&s, &go, i] {
                int v = i * 25;
                while (go.load()) {
                    s->feed(v);
                    v = (v + 1) % 101;
                }
            });
        }
        sleep_ms(250);
        go = false;
        for (auto& th : workers) th.join();
        s->stop(false);
        bool all_valid = true;
        for (const auto& w : t.snapshot()) {
            if (w.bytes.size() != 2 || w.bytes[0] != 0xA5 || w.bytes[1] > 100) all_valid = false;
        }
        check(all_valid, "every streamed frame is a well-formed [0xA5, 0-100]");
    }

    TEST("reward_event and the stream never interleave a partial write");
    {
        MockTransport t;
        Glasses g(&t);
        auto s = g.start_feedback_stream(45.0);
        std::atomic<bool> go{true};
        std::thread feeder([&s, &go] {
            int v = 0;
            while (go.load()) {
                s->feed(v);
                v = (v + 7) % 101;
            }
        });
        for (int i = 0; i < 30; ++i) {
            s->reward_event(0, 0);
            sleep_ms(5);
        }
        go = false;
        feeder.join();
        s->stop(false);
        bool all_valid = true;
        for (const auto& w : t.snapshot()) {
            if (w.bytes.size() != 2 || w.bytes[0] != 0xA5 || w.bytes[1] > 100) all_valid = false;
        }
        check(all_valid, "all frames well-formed under concurrent reward + stream");
    }
}

// ---------------------------------------------------------------------------
// 8. CallbackTransport — the bring-your-own-BLE path
// ---------------------------------------------------------------------------

static void test_callback_transport() {
    std::cout << "\nCallbackTransport\n";

    TEST("frames reach a user-supplied write callback");
    std::vector<std::vector<std::uint8_t>> seen;
    std::vector<bool> acked;
    bool connected = true;
    edge::CallbackTransport ct(
        [&](const std::uint8_t* d, std::size_t n, bool ack) {
            seen.emplace_back(d, d + n);
            acked.push_back(ack);
        },
        [&] { return connected; });

    Glasses g(&ct);
    g.set_static(50);
    g.set_opacity(200);
    check(seen.size() == 2, "two writes observed");
    if (seen.size() == 2) {
        expect_bytes(seen[0], {0xA5, 50}, "set_static");
        expect_bytes(seen[1], {0xC8}, "set_opacity as a single byte");
    }
    check(acked.size() == 2 && acked[0] && acked[1], "both with-response");

    TEST("write-without-response is opt-in");
    check(!g.supports_fast_write(), "off by default");
    ct.set_supports_write_without_response(true);
    check(g.supports_fast_write(), "on once declared");
    seen.clear();
    acked.clear();
    g.stream_static(10);
    check(!acked.empty() && !acked[0], "streaming write is unacked once declared");

    TEST("battery is absent without a reader");
    check(!g.get_battery().has_value(), "nullopt without a battery callback");
    ct.set_battery_reader([](int& out) {
        out = 42;
        return true;
    });
    check(g.get_battery().value_or(-1) == 42, "reader is used once supplied");

    TEST("a disconnected callback transport raises ConnectionError");
    connected = false;
    bool threw = false;
    try {
        g.set_static(10);
    } catch (const edge::ConnectionError&) {
        threw = true;
    }
    check(threw, "ConnectionError");
}

// ---------------------------------------------------------------------------

int main() {
    std::cout << "EDGE Glasses C++ SDK — test suite\n";
    std::cout << "=================================\n";

    test_protocol_frames();
    test_two_byte_rule();
    test_glasses_commands();
    test_presets();
    test_connection_and_errors();
    test_fast_write();
    test_feedback_stream();
    test_callback_transport();

    std::cout << "\n---------------------------------\n";
    std::cout << g_checks << " checks, " << g_failures << " failed\n";
    if (g_failures == 0) std::cout << "ALL TESTS PASSED\n";
    return g_failures == 0 ? 0 : 1;
}
