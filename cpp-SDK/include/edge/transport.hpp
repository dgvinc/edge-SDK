// SPDX-License-Identifier: MIT
//
// EDGE Glasses — BLE transport abstraction.
//
// Glasses talks to the device through this interface, so the SDK is not tied to
// any one Bluetooth stack. Two implementations ship with it:
//
//   * WinRtTransport   — Windows 10/11, built on Windows.Devices.Bluetooth
//                        (winrt_transport.hpp; MSVC only)
//   * CallbackTransport — adapts any existing GATT stack via two function
//                        pointers, which is also what the C API uses
//
// Implementing your own is three methods. The SDK guarantees it never issues
// overlapping writes, so an implementation does not need to be re-entrant.

#ifndef EDGE_TRANSPORT_HPP
#define EDGE_TRANSPORT_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "edge/errors.hpp"

namespace edge {

/// A device discovered by a scan.
struct ScanResult {
    std::string name;     ///< always "Narbis_Edge"
    std::string address;  ///< platform address string ("AA:BB:CC:DD:EE:FF" on Windows)
    int rssi = -100;      ///< signal strength, dBm

    std::string to_string() const {
        return name + " (" + address + ") RSSI: " + std::to_string(rssi);
    }
};

/// Abstract BLE link to one pair of glasses.
///
/// All methods are called from at most one thread at a time: Glasses serializes
/// every write, and FeedbackStream shares that same serialization.
class ITransport {
public:
    virtual ~ITransport() = default;

    /// True while a GATT connection is established.
    virtual bool is_connected() const = 0;

    /// Establish the link, for transports that own their connection.
    ///
    /// The default is a no-op, which is correct for transports wrapping a link
    /// your application already opened (CallbackTransport). WinRtTransport
    /// overrides it to scan for "Narbis_Edge" and connect.
    ///
    /// @param timeout_ms overall budget for scan + connect
    /// @throws DeviceNotFoundError, ConnectionError, TimeoutError
    virtual void connect(int timeout_ms) { (void)timeout_ms; }

    /// Tear down the link, for transports that own their connection.
    /// The default is a no-op. Never throws.
    virtual void disconnect() {}

    /// Write bytes to the control characteristic 0xFF01.
    ///
    /// @param data  frame bytes (already clamped and padded by the caller)
    /// @param len   frame length; 1 only for the legacy opacity write
    /// @param with_response true for write-with-response (ordering + back-pressure).
    ///        The SDK passes false only on the high-rate streaming path, and only
    ///        when supports_write_without_response() is true.
    /// @throws CommandError if the write does not reach the device
    virtual void write(const std::uint8_t* data, std::size_t len, bool with_response) = 0;

    /// True if 0xFF01 advertises write-without-response (firmware >= 4.16.3).
    ///
    /// When true the streaming path skips the per-write ATT round-trip, lifting
    /// sustained throughput past the ~20 writes/sec that acked writes allow.
    /// Older firmware is write-with-response only; return false there.
    virtual bool supports_write_without_response() const = 0;

    /// Read Battery Level (0x2A19) from the standard Battery Service (0x180F).
    ///
    /// @param out_level receives 0-100 on success
    /// @return false if the device does not expose 0x180F (pre-4.16.1 firmware,
    ///         or a board built without the battery divider)
    /// @throws CommandError if the service exists but the read fails
    virtual bool read_battery_level(int& out_level) = 0;
};

/// Adapts an existing GATT stack to ITransport with plain callbacks.
///
/// Use this when your application already owns the BLE connection — a Qt,
/// SimpleBLE, BlueZ, or in-house stack — and you only want the SDK's protocol
/// encoding, clamping, and streaming logic. This is also the transport the C API
/// exposes to pure-C callers.
///
///     edge::CallbackTransport t(
///         [&](const std::uint8_t* d, std::size_t n, bool ack) { my_write(d, n, ack); },
///         [&] { return my_is_connected(); });
///     edge::Glasses g(&t);
///     g.set_static(50);
class CallbackTransport : public ITransport {
public:
    /// Writes a frame to 0xFF01. Throw (or set connected false) to signal failure.
    using WriteFn = std::function<void(const std::uint8_t* data, std::size_t len, bool with_response)>;
    /// Reports whether the link is up.
    using ConnectedFn = std::function<bool()>;
    /// Optional battery read; return false when 0x180F is absent.
    using BatteryFn = std::function<bool(int& out_level)>;

    CallbackTransport(WriteFn write_fn, ConnectedFn connected_fn)
        : write_(std::move(write_fn)), connected_(std::move(connected_fn)) {}

    bool is_connected() const override { return connected_ && connected_(); }

    void write(const std::uint8_t* data, std::size_t len, bool with_response) override {
        if (!write_) throw CommandError("CallbackTransport has no write callback");
        write_(data, len, with_response);
    }

    bool supports_write_without_response() const override { return fast_write_; }

    /// Declare that 0xFF01 advertises write-without-response on this link
    /// (firmware >= 4.16.3). Off by default, which is always safe.
    void set_supports_write_without_response(bool supported) { fast_write_ = supported; }

    /// Optional: supply a battery reader. Without one, get_battery() reports
    /// "not available on this unit".
    void set_battery_reader(BatteryFn fn) { battery_ = std::move(fn); }

    bool read_battery_level(int& out_level) override {
        if (!battery_) return false;
        return battery_(out_level);
    }

private:
    WriteFn write_;
    ConnectedFn connected_;
    BatteryFn battery_;
    bool fast_write_ = false;
};

}  // namespace edge

#endif  // EDGE_TRANSPORT_HPP
