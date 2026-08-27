// SPDX-License-Identifier: MIT

#include "edge/feedback_stream.hpp"

#include <cmath>

namespace edge {
namespace {

int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

double clamp_double(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

FeedbackStream::FeedbackStream(Glasses& glasses, double rate_hz) : glasses_(glasses) {
    if (!(rate_hz == rate_hz)) rate_hz = 30.0;  // NaN -> default
    rate_hz_ = clamp_double(rate_hz, kMinRateHz, kMaxRateHz);
    interval_ = std::chrono::nanoseconds(
        static_cast<std::int64_t>(1e9 / rate_hz_));
    running_ = true;
    thread_ = std::thread(&FeedbackStream::run, this);
}

FeedbackStream::~FeedbackStream() {
    // Leaving a wearer dark is the bad failure mode, so the destructor clears
    // by default. Errors are suppressed: a destructor must not throw.
    try {
        stop(true);
    } catch (...) {
    }
}

std::int64_t FeedbackStream::now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void FeedbackStream::feed(int duty) { duty_.store(clamp_int(duty, 0, 100)); }

void FeedbackStream::feed_reward(double value) {
    if (!(value == value)) value = 0.0;  // NaN -> fully dark (out of condition)
    value = clamp_double(value, 0.0, 1.0);
    feed(static_cast<int>(std::lround((1.0 - value) * 100.0)));
}

void FeedbackStream::write_locked(int duty) {
    last_sent_ = duty;  // claim before the write, so a concurrent tick coalesces
    try {
        glasses_.stream_static(duty);
    } catch (...) {
        last_sent_ = -1;  // failed write: retry on the next tick
    }
}

void FeedbackStream::reward_event(int duty, int hold_ms) {
    duty = clamp_int(duty, 0, 100);
    {
        // Blocking: waits out at most one in-flight tick write, never a queue.
        std::lock_guard<std::mutex> lock(stream_mutex_);
        write_locked(duty);
    }
    if (hold_ms > 0) {
        hold_until_ns_.store(now_ns() + static_cast<std::int64_t>(hold_ms) * 1000000LL);
    }
}

void FeedbackStream::run() {
    while (!stop_requested_.load()) {
        const int duty = duty_.load();
        if (duty >= 0 && now_ns() >= hold_until_ns_.load()) {
            // try_lock, not lock: a reward_event() in progress owns the wire,
            // and this tick yields to it rather than queueing behind it.
            std::unique_lock<std::mutex> lock(stream_mutex_, std::try_to_lock);
            if (lock.owns_lock() && duty != last_sent_) {
                write_locked(duty);
            }
        }
        std::unique_lock<std::mutex> wake(wake_mutex_);
        wake_cv_.wait_for(wake, interval_, [this] { return stop_requested_.load(); });
    }
    running_ = false;
}

void FeedbackStream::stop(bool clear_lens) {
    if (stop_requested_.exchange(true)) return;  // idempotent
    {
        std::lock_guard<std::mutex> wake(wake_mutex_);
    }
    wake_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    running_ = false;
    if (clear_lens && glasses_.is_connected()) {
        glasses_.clear();
    }
}

bool FeedbackStream::is_running() const { return running_.load(); }

double FeedbackStream::rate_hz() const { return rate_hz_; }

}  // namespace edge
