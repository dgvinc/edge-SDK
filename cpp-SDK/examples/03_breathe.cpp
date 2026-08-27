// SPDX-License-Identifier: MIT
//
// Paced breathing with the on-board breathe engine.
//
// For a periodic waveform, do NOT stream per-tick opacity: send a handful of
// parameter writes once and let the glasses render the 100 Hz cosine locally.
// The link then carries only occasional writes and the waveform stays smooth
// regardless of BLE conditions.
//
// The second half shows fractional breathing rates. 0xB1 is integer-BPM only,
// so an exact rate like 5.5 br/min needs sync_breath(), which sets the precise
// cycle length AND restarts the cosine at the moment of the write.
//
// The one rule: send sync_breath ONLY at a breath-cycle boundary (the start of
// an inhale), never mid-breath. Re-anchoring mid-inhale teleports the
// firmware's phase and the wearer sees a visible snap. The override also
// auto-expires two cycles after the last write, so re-sending each breath is
// required, not optional.

#include <chrono>
#include <iostream>
#include <thread>

#include "edge/glasses.hpp"
#include "connect_helper.hpp"

int main() {
    auto transport = example::make_transport();
    if (!transport) return 0;

    try {
        edge::Glasses glasses(std::move(transport));
        std::cout << "Scanning for " << edge::kDeviceName << "...\n";
        glasses.connect(15000);
        std::cout << "Connected.\n";

        glasses.set_duration(60);

        // --- Part 1: integer-BPM breathing, entirely on-device --------------
        std::cout << "\n6 BPM sine breathing for 20 s (rendered on the glasses).\n";
        glasses.set_brightness(100);  // depth: peak darkness at full inhale

        edge::BreatheOptions options;
        options.bpm = 6;
        options.inhale_pct = 40;   // 40 % inhale / 60 % exhale
        options.waveform = edge::Waveform::Sine;
        glasses.start_breathe(options);

        std::this_thread::sleep_for(std::chrono::seconds(20));

        // --- Part 2: a fractional rate, phase-locked to this app's clock ----
        // 5.5 breaths/min = a 10909 ms cycle. Integer BPM cannot express it.
        const int cycle_ms = 10909;
        const int inhale_pct = 40;
        const int breaths = 4;

        std::cout << "\n5.5 BPM (" << cycle_ms << " ms cycle), phase-locked, "
                  << breaths << " breaths.\n"
                  << "Drive your on-screen cue and audio chime off this same clock.\n";

        // 0xB1 must always accompany 0xBA: it is the integer-rate fallback the
        // engine reverts to when the sync expires (and on firmware < 4.15.5,
        // which ignores 0xBA entirely). round(60000 / 10909) = 6 BPM. There is
        // no dedicated setter for a bare 0xB1 - start_breathe() would re-send
        // the mode byte too - so use the documented low-level escape hatch.
        const std::uint8_t nearest_bpm = 6;

        for (int i = 0; i < breaths; ++i) {
            // BREATH BOUNDARY: the only place these writes belong.
            glasses.sync_breath(cycle_ms, inhale_pct);        // exact cycle + phase anchor
            glasses.send_command(0xB1, {nearest_bpm});        // integer-rate fallback
            std::cout << "  breath " << (i + 1) << " anchored\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(cycle_ms));
        }

        std::cout << "\nClearing and disconnecting.\n";
        glasses.clear();
        glasses.disconnect();
        return 0;

    } catch (const edge::DeviceNotFoundError&) {
        example::explain_not_found();
        return 1;
    } catch (const edge::GlassesError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
