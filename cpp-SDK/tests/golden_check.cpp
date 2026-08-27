// SPDX-License-Identifier: MIT
//
// Cross-language golden-vector check.
//
// Replays the exact call script from tools/golden/gen_golden.py through the C++
// SDK and asserts every frame is byte-identical to what the reference Python
// SDK produced. Any divergence in clamping, padding, byte order, or the
// write-with/without-response decision fails here rather than on a customer's
// glasses.
//
//   ./golden_check ../../tools/golden/golden_frames.txt

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "edge/glasses.hpp"
#include "mock_transport.hpp"

using edge::BreatheOptions;
using edge::Glasses;
using edge::Waveform;
using edge::test::MockTransport;

namespace {

struct Record {
    std::string step;
    std::vector<std::uint8_t> bytes;
    bool with_response = true;
};

std::string to_hex(const std::vector<std::uint8_t>& v) {
    std::ostringstream os;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) os << ' ';
        os << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
           << static_cast<int>(v[i]);
    }
    return os.str();
}

std::vector<Record> load_golden(const std::string& path, bool& ok) {
    std::vector<Record> out;
    std::ifstream in(path);
    if (!in) {
        std::cerr << "cannot open golden vector file: " << path << "\n";
        ok = false;
        return out;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        const std::size_t t1 = line.find('\t');
        const std::size_t t2 = line.find('\t', t1 == std::string::npos ? 0 : t1 + 1);
        if (t1 == std::string::npos || t2 == std::string::npos) {
            std::cerr << "malformed golden line: " << line << "\n";
            ok = false;
            continue;
        }
        Record r;
        r.step = line.substr(0, t1);
        const std::string hexed = line.substr(t1 + 1, t2 - t1 - 1);
        const std::string flag = line.substr(t2 + 1);
        std::istringstream hs(hexed);
        std::string byte_text;
        while (hs >> byte_text) {
            r.bytes.push_back(static_cast<std::uint8_t>(std::strtoul(byte_text.c_str(), nullptr, 16)));
        }
        r.with_response = (flag != "N");
        out.push_back(r);
    }
    ok = ok && !out.empty();
    return out;
}

/// One named step of the shared call script.
struct Step {
    const char* name;
    std::function<void(Glasses&, MockTransport&)> run;
};

std::vector<Step> build_script() {
    // Mirrors tools/golden/gen_golden.py step for step, in the same order.
    return {
        // opacity — the legacy single-byte write
        {"opacity_0", [](Glasses& g, MockTransport&) { g.set_opacity(0); }},
        {"opacity_128", [](Glasses& g, MockTransport&) { g.set_opacity(128); }},
        {"opacity_255", [](Glasses& g, MockTransport&) { g.set_opacity(255); }},
        {"opacity_clamp_low", [](Glasses& g, MockTransport&) { g.set_opacity(-1); }},
        {"opacity_clamp_high", [](Glasses& g, MockTransport&) { g.set_opacity(999); }},
        {"clear", [](Glasses& g, MockTransport&) { g.clear(); }},
        {"dark", [](Glasses& g, MockTransport&) { g.dark(); }},

        // static duty
        {"static_0", [](Glasses& g, MockTransport&) { g.set_static(0); }},
        {"static_50", [](Glasses& g, MockTransport&) { g.set_static(50); }},
        {"static_100", [](Glasses& g, MockTransport&) { g.set_static(100); }},
        {"static_clamp_low", [](Glasses& g, MockTransport&) { g.set_static(-5); }},
        {"static_clamp_high", [](Glasses& g, MockTransport&) { g.set_static(255); }},

        // parameters
        {"brightness_80", [](Glasses& g, MockTransport&) { g.set_brightness(80); }},
        {"brightness_clamp_low", [](Glasses& g, MockTransport&) { g.set_brightness(-1); }},
        {"brightness_clamp_high", [](Glasses& g, MockTransport&) { g.set_brightness(101); }},
        {"duration_60", [](Glasses& g, MockTransport&) { g.set_duration(60); }},
        {"duration_clamp_low", [](Glasses& g, MockTransport&) { g.set_duration(0); }},
        {"duration_clamp_high", [](Glasses& g, MockTransport&) { g.set_duration(120); }},
        {"strobe_freq_10", [](Glasses& g, MockTransport&) { g.set_strobe_frequency(10); }},
        {"strobe_freq_clamp_low", [](Glasses& g, MockTransport&) { g.set_strobe_frequency(0); }},
        {"strobe_freq_clamp_high", [](Glasses& g, MockTransport&) { g.set_strobe_frequency(99); }},
        {"strobe_duty_50", [](Glasses& g, MockTransport&) { g.set_strobe_duty(50); }},
        {"strobe_duty_clamp_low", [](Glasses& g, MockTransport&) { g.set_strobe_duty(0); }},
        {"strobe_duty_clamp_high", [](Glasses& g, MockTransport&) { g.set_strobe_duty(100); }},

        // lens config (fw >= 4.15.7)
        {"smoothing_0", [](Glasses& g, MockTransport&) { g.set_lens_smoothing(0); }},
        {"smoothing_80", [](Glasses& g, MockTransport&) { g.set_lens_smoothing(80); }},
        {"smoothing_85_truncates", [](Glasses& g, MockTransport&) { g.set_lens_smoothing(85); }},
        {"smoothing_2550", [](Glasses& g, MockTransport&) { g.set_lens_smoothing(2550); }},
        {"smoothing_clamp_high", [](Glasses& g, MockTransport&) { g.set_lens_smoothing(9999); }},
        {"smoothing_clamp_low", [](Glasses& g, MockTransport&) { g.set_lens_smoothing(-50); }},
        {"max_rate_5", [](Glasses& g, MockTransport&) { g.set_lens_max_rate(5); }},
        {"max_rate_clamp_low", [](Glasses& g, MockTransport&) { g.set_lens_max_rate(-1); }},
        {"max_rate_clamp_high", [](Glasses& g, MockTransport&) { g.set_lens_max_rate(200); }},
        {"disconnect_freeze", [](Glasses& g, MockTransport&) { g.set_disconnect_behavior(false); }},
        {"disconnect_fail_clear",
         [](Glasses& g, MockTransport&) { g.set_disconnect_behavior(true); }},

        // modes
        {"strobe_start_bare", [](Glasses& g, MockTransport&) { g.start_strobe(); }},
        {"strobe_start_full", [](Glasses& g, MockTransport&) { g.start_strobe(10, 50); }},
        {"strobe_start_duty_only",
         [](Glasses& g, MockTransport&) { g.start_strobe(std::nullopt, 40); }},
        {"breathe_bare", [](Glasses& g, MockTransport&) { g.start_breathe(); }},
        {"breathe_full",
         [](Glasses& g, MockTransport&) {
             BreatheOptions o;
             o.bpm = 5;
             o.inhale_pct = 40;
             o.hold_top_ms = 1000;
             o.hold_bottom_ms = 500;
             o.waveform = Waveform::Sine;
             g.start_breathe(o);
         }},
        {"breathe_linear",
         [](Glasses& g, MockTransport&) {
             BreatheOptions o;
             o.waveform = Waveform::Linear;
             g.start_breathe(o);
         }},
        {"breathe_with_strobe",
         [](Glasses& g, MockTransport&) {
             BreatheOptions o;
             o.bpm = 8;
             o.with_strobe = true;
             g.start_breathe(o);
         }},
        {"breathe_clamps",
         [](Glasses& g, MockTransport&) {
             BreatheOptions o;
             o.bpm = 99;
             o.inhale_pct = 5;
             o.hold_top_ms = 9999;
             g.start_breathe(o);
         }},

        // breathe sync (u16 little-endian cycle length)
        {"sync_5500", [](Glasses& g, MockTransport&) { g.sync_breath(5500); }},
        {"sync_5500_inhale50", [](Glasses& g, MockTransport&) { g.sync_breath(5500, 50); }},
        {"sync_10000", [](Glasses& g, MockTransport&) { g.sync_breath(10000, 50); }},
        {"sync_255", [](Glasses& g, MockTransport&) { g.sync_breath(255); }},
        {"sync_256", [](Glasses& g, MockTransport&) { g.sync_breath(256); }},
        {"sync_clamp_u16", [](Glasses& g, MockTransport&) { g.sync_breath(70000); }},
        {"sync_clamp_negative", [](Glasses& g, MockTransport&) { g.sync_breath(-1); }},
        {"sync_inhale_clamp_low", [](Glasses& g, MockTransport&) { g.sync_breath(5000, 5); }},
        {"sync_inhale_clamp_high", [](Glasses& g, MockTransport&) { g.sync_breath(5000, 99); }},

        // power / maintenance
        {"sleep", [](Glasses& g, MockTransport&) { g.sleep(); }},
        {"factory_reset", [](Glasses& g, MockTransport&) { g.factory_reset(); }},

        // low-level escape hatch: the >= 2-byte rule
        {"cmd_no_payload", [](Glasses& g, MockTransport&) { g.send_command(0xA6); }},
        {"cmd_sleep_no_payload", [](Glasses& g, MockTransport&) { g.send_command(0xA7); }},
        {"cmd_with_payload", [](Glasses& g, MockTransport&) { g.send_command(0xA2, {80}); }},
        {"cmd_three_byte",
         [](Glasses& g, MockTransport&) { g.send_command(0xBA, {0x7C, 0x15, 40}); }},

        // preset sessions
        {"session_relax", [](Glasses& g, MockTransport&) { g.session_relax(10); }},
        {"session_meditate", [](Glasses& g, MockTransport&) { g.session_meditate(10); }},
        {"session_focus", [](Glasses& g, MockTransport&) { g.session_focus(15); }},
        {"session_sleep", [](Glasses& g, MockTransport&) { g.session_sleep(15); }},

        // streaming path: the response flag depends on fw >= 4.16.3
        {"stream_static_slow_0",
         [](Glasses& g, MockTransport& t) {
             t.set_fast_write(false);
             g.stream_static(0);
         }},
        {"stream_static_slow_42",
         [](Glasses& g, MockTransport& t) {
             t.set_fast_write(false);
             g.stream_static(42);
         }},
        {"stream_static_slow_100",
         [](Glasses& g, MockTransport& t) {
             t.set_fast_write(false);
             g.stream_static(100);
         }},
        {"stream_static_clamp_low",
         [](Glasses& g, MockTransport& t) {
             t.set_fast_write(false);
             g.stream_static(-10);
         }},
        {"stream_static_clamp_high",
         [](Glasses& g, MockTransport& t) {
             t.set_fast_write(false);
             g.stream_static(500);
         }},
        {"stream_static_fast_42",
         [](Glasses& g, MockTransport& t) {
             t.set_fast_write(true);
             g.stream_static(42);
         }},
        {"stream_static_fast_0",
         [](Glasses& g, MockTransport& t) {
             t.set_fast_write(true);
             g.stream_static(0);
         }},
    };
}

}  // namespace

int main(int argc, char** argv) {
    const std::string path =
        argc > 1 ? argv[1] : std::string("../../tools/golden/golden_frames.txt");

    bool ok = true;
    const std::vector<Record> expected = load_golden(path, ok);
    if (!ok) {
        std::cerr << "failed to load golden vectors\n";
        return 2;
    }

    MockTransport transport;
    Glasses glasses(&transport);

    std::vector<Record> actual;
    for (const Step& step : build_script()) {
        transport.take_writes();
        step.run(glasses, transport);
        for (const auto& w : transport.take_writes()) {
            Record r;
            r.step = step.name;
            r.bytes = w.bytes;
            r.with_response = w.with_response;
            actual.push_back(r);
        }
    }

    std::cout << "golden vectors: " << expected.size() << " expected, " << actual.size()
              << " produced\n";

    int failures = 0;
    const std::size_t n = expected.size() < actual.size() ? expected.size() : actual.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Record& e = expected[i];
        const Record& a = actual[i];
        if (e.step != a.step || e.bytes != a.bytes || e.with_response != a.with_response) {
            ++failures;
            std::cout << "  MISMATCH at frame " << i << "\n"
                      << "    python: " << e.step << "  [" << to_hex(e.bytes) << "]  "
                      << (e.with_response ? "with-response" : "without-response") << "\n"
                      << "    c++   : " << a.step << "  [" << to_hex(a.bytes) << "]  "
                      << (a.with_response ? "with-response" : "without-response") << "\n";
        }
    }
    if (expected.size() != actual.size()) {
        ++failures;
        std::cout << "  FRAME COUNT MISMATCH: python produced " << expected.size() << ", C++ "
                  << actual.size() << "\n";
    }

    if (failures == 0) {
        std::cout << "GOLDEN VECTORS MATCH — C++ is byte-identical to the Python SDK\n";
        return 0;
    }
    std::cout << failures << " mismatch(es)\n";
    return 1;
}
