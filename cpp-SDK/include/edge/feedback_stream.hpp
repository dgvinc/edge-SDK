// SPDX-License-Identifier: MIT
//
// EDGE Glasses — push-style real-time lens control (a wearable screen dimmer).
//
// Created via Glasses::start_feedback_stream(). Call feed() / feed_reward()
// from anywhere — a BLE notification handler, an LSL callback, a UDP reader,
// your DSP thread — at any rate; a background writer decimates to the stream
// rate, skips unchanged values, and never overlaps BLE writes.
//
//     auto stream = glasses.start_feedback_stream();     // ~30 Hz writer
//     // from your pipeline, any thread, any rate:
//     stream->feed_reward(value);                        // 0..1, 1 = in condition
//     // the instant a contingency is met:
//     stream->reward_event(0, 150);                      // discrete reward, now
//     stream->stop();                                    // stops and clears the lens

#ifndef EDGE_FEEDBACK_STREAM_HPP
#define EDGE_FEEDBACK_STREAM_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "edge/glasses.hpp"

namespace edge {

/// Real-time lens stream: coalescing, decimating, serialized writer.
///
/// A failed write resets the coalesce key so the next tick retries. The stream
/// must not outlive the Glasses object that created it.
class FeedbackStream {
public:
    /// Lowest writer rate, Hz.
    static constexpr double kMinRateHz = 1.0;
    /// Highest writer rate, Hz. Above this the BLE link, not the timer, is the
    /// limit — see the rate contract in the protocol doc §4.6.1.
    static constexpr double kMaxRateHz = 45.0;

    /// Prefer Glasses::start_feedback_stream().
    /// @param glasses owner; must outlive this stream
    /// @param rate_hz writer rate, clamped to [kMinRateHz, kMaxRateHz]
    FeedbackStream(Glasses& glasses, double rate_hz = 30.0);

    /// Stops the writer and clears the lens (errors suppressed). Call stop()
    /// explicitly if you need to observe failures or keep the last tint.
    ~FeedbackStream();

    FeedbackStream(const FeedbackStream&) = delete;
    FeedbackStream& operator=(const FeedbackStream&) = delete;

    /// Request a lens duty: 0 = clear .. 100 = fully dark.
    ///
    /// Cheap, lock-free and safe to call at any rate from any thread; only
    /// changed values reach BLE. Use this for PROPORTIONAL feedback (a dimmer
    /// that tracks your signal).
    ///
    /// @param duty 0-100 (clamped)
    void feed(int duty);

    /// Request tint from a 0..1 reward value (1 = in condition = clear).
    /// The classic dimmer mapping: duty = (1 - value) * 100.
    /// @param value 0.0-1.0 (clamped; NaN is treated as 0)
    void feed_reward(double value);

    /// Deliver a DISCRETE reward NOW, bypassing the stream tick.
    ///
    /// For operant conditioning: call the instant your detector crosses
    /// threshold. Unlike feed(), which parks the value for the next scheduled
    /// tick (up to one stream period later), this writes immediately — latency
    /// is just the BLE transport (~20-60 ms), with no cadence jitter. It
    /// preempts the proportional stream, waiting at most one in-flight write
    /// (it never queues behind routine dimmer updates).
    ///
    /// Blocks until the write completes. Write failures are swallowed, matching
    /// the stream's retry-next-tick behaviour.
    ///
    /// @param duty reward tint 0-100 (default 0 = fully clear = positive reward)
    /// @param hold_ms hold the reward tint this long before the proportional
    ///        stream resumes (0 = let the next feed() value take back over)
    void reward_event(int duty = 0, int hold_ms = 0);

    /// Stop the writer thread.
    ///
    /// @param clear_lens by default clears the lens — it otherwise FREEZES at
    ///        the last tint across a disconnect (protocol doc §2.5).
    /// Idempotent; a second call does nothing.
    void stop(bool clear_lens = true);

    /// True while the writer thread is running.
    bool is_running() const;

    /// Configured writer rate in Hz, after clamping.
    double rate_hz() const;

private:
    void run();
    /// Write one duty value. Caller must hold stream_mutex_.
    void write_locked(int duty);
    static std::int64_t now_ns();

    Glasses& glasses_;
    std::chrono::nanoseconds interval_;
    double rate_hz_;

    /// Latest requested duty, or -1 before the first feed(). Written by feed()
    /// from any thread, read by the writer thread.
    std::atomic<int> duty_{-1};
    /// steady_clock nanoseconds until which a reward tint holds off the stream.
    std::atomic<std::int64_t> hold_until_ns_{0};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    /// Guards last_sent_ and gives reward_event() priority over the writer tick:
    /// reward_event() blocks on it, the writer thread try_locks and skips a tick
    /// rather than queueing behind a reward. Always acquired before the Glasses
    /// write mutex, never after.
    std::mutex stream_mutex_;
    int last_sent_ = -1;

    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;
    std::thread thread_;
};

}  // namespace edge

#endif  // EDGE_FEEDBACK_STREAM_HPP
