// SPDX-License-Identifier: MIT
//
// A recording ITransport for tests: captures every frame the SDK puts on the
// wire, so assertions can compare exact bytes against the protocol document.

#ifndef EDGE_TESTS_MOCK_TRANSPORT_HPP
#define EDGE_TESTS_MOCK_TRANSPORT_HPP

#include <mutex>
#include <string>
#include <vector>

#include "edge/transport.hpp"

namespace edge {
namespace test {

/// One recorded GATT write.
struct Write {
    std::vector<std::uint8_t> bytes;
    bool with_response = true;
};

class MockTransport : public ITransport {
public:
    bool is_connected() const override { return connected_; }

    void connect(int timeout_ms) override {
        (void)timeout_ms;
        ++connect_calls;
        connected_ = true;
    }

    void disconnect() override {
        ++disconnect_calls;
        connected_ = false;
    }

    void write(const std::uint8_t* data, std::size_t len, bool with_response) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fail_next_ > 0) {
            --fail_next_;
            ++failed_writes;
            throw CommandError("mock: injected write failure");
        }
        Write w;
        w.bytes.assign(data, data + len);
        w.with_response = with_response;
        writes.push_back(w);
    }

    bool supports_write_without_response() const override { return fast_write_; }

    bool read_battery_level(int& out_level) override {
        if (!has_battery_) return false;
        out_level = battery_level_;
        return true;
    }

    // ---- test controls -----------------------------------------------------

    void set_connected(bool v) { connected_ = v; }
    void set_fast_write(bool v) { fast_write_ = v; }
    void set_battery(bool present, int level = 0) {
        has_battery_ = present;
        battery_level_ = level;
    }
    /// Make the next `n` writes throw CommandError.
    void fail_next_writes(int n) { fail_next_ = n; }

    /// Drain the recorded writes.
    std::vector<Write> take_writes() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Write> out;
        out.swap(writes);
        return out;
    }

    /// Copy the recorded writes without draining them.
    std::vector<Write> snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        return writes;
    }

    std::size_t write_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        return writes.size();
    }

    int connect_calls = 0;
    int disconnect_calls = 0;
    int failed_writes = 0;

private:
    // Always behind mutex_: the FeedbackStream writer thread records here while
    // the test thread reads.
    std::vector<Write> writes;
    mutable std::mutex mutex_;
    bool connected_ = true;
    bool fast_write_ = false;
    bool has_battery_ = false;
    int battery_level_ = 0;
    int fail_next_ = 0;
};

}  // namespace test
}  // namespace edge

#endif  // EDGE_TESTS_MOCK_TRANSPORT_HPP
