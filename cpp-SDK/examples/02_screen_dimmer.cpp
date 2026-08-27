// SPDX-License-Identifier: MIT
//
// The wearable screen dimmer — the primary third-party integration pattern.
//
// Classic neurofeedback dims the training display when the trainee falls out of
// condition and clears it when they are in condition. The Edge does the same on
// the lens itself, so it drops into any protocol (SMR, alpha/theta, HEG, EMG
// down-training, HRV...) wherever your software already produces a 0..1
// feedback value.
//
// Wire your pipeline's callback straight to stream->feed_reward(). The stream
// handles decimation, coalescing and write serialization; you never think about
// BLE cadence. For a DISCRETE operant reward, call reward_event() instead — it
// bypasses the tick so reinforcement latency is transport-only.
//
// This example substitutes a synthetic signal for a real EEG index.

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

#include "edge/feedback_stream.hpp"
#include "edge/glasses.hpp"
#include "connect_helper.hpp"

namespace {

/// Stand-in for your pipeline: a slow oscillation with a threshold crossing.
/// Replace with your real feedback value, 0..1 (1 = in condition).
double synthetic_feedback(double seconds) {
    const double slow = 0.5 + 0.5 * std::sin(seconds * 0.6);
    const double jitter = 0.05 * std::sin(seconds * 7.3);
    double v = slow + jitter;
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

}  // namespace

int main() {
    auto transport = example::make_transport();
    if (!transport) return 0;

    try {
        edge::Glasses glasses(std::move(transport));
        std::cout << "Scanning for " << edge::kDeviceName << "...\n";
        glasses.connect(15000);
        std::cout << "Connected.\n";

        // 1. Session guard: the device deep-sleeps when the 0xA4 timer expires.
        glasses.set_duration(60);

        // 2. On-device smoothing so the lens glides between writes instead of
        //    stepping, and per-sample noise is absorbed without client-side
        //    filtering. Persisted; ignored by firmware < 4.15.7, so it is
        //    always safe to send. Rule of thumb: tau ~= 1-2x the write period.
        glasses.set_lens_smoothing(80);

        // 3. Safety envelope: the lens can never snap, even if this process
        //    streams garbage. Comment out for minimum-latency discrete rewards.
        glasses.set_lens_max_rate(40);

        // 4. Fail clear on link loss, so a crash does not leave the wearer dark
        //    (fw >= 4.15.7). Bounded by the ~32 s supervision timeout, so the
        //    explicit clear before disconnect below still matters.
        glasses.set_disconnect_behavior(true);

        std::cout << "Fast write (fw >= 4.16.3): "
                  << (glasses.supports_fast_write() ? "yes" : "no") << "\n";

        // 5. Open the stream. Push from any thread at any rate.
        auto stream = glasses.start_feedback_stream(30.0);
        std::cout << "Streaming at " << stream->rate_hz() << " Hz for 30 s.\n"
                  << "(In your app, call feed_reward() from your pipeline callback.)\n";

        const auto started = std::chrono::steady_clock::now();
        bool was_in_condition = false;

        while (true) {
            const double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            if (seconds > 30.0) break;

            const double value = synthetic_feedback(seconds);

            // Proportional feedback: the tint tracks the signal continuously.
            stream->feed_reward(value);

            // Discrete reward: fire the instant a contingency is met. This
            // preempts the proportional stream and holds the reward tint
            // briefly, so reinforcement is not gated by the stream cadence.
            const bool in_condition = value > 0.85;
            if (in_condition && !was_in_condition) {
                stream->reward_event(/*duty=*/0, /*hold_ms=*/300);
                std::cout << "  reward at t=" << static_cast<int>(seconds) << " s\n";
            }
            was_in_condition = in_condition;

            // Your real loop is driven by your pipeline's callback, not a sleep.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        // stop() clears the lens by default: it otherwise freezes at the last
        // tint until session expiry.
        stream->stop();
        glasses.disconnect();
        std::cout << "Done.\n";
        return 0;

    } catch (const edge::DeviceNotFoundError&) {
        example::explain_not_found();
        return 1;
    } catch (const edge::GlassesError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
