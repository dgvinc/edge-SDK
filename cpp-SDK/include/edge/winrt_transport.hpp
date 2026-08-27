// SPDX-License-Identifier: MIT
//
// EDGE Glasses — Windows BLE transport (C++/WinRT).
//
// Built on Windows.Devices.Bluetooth, the same stack the C# SDK uses. Requires
// MSVC (C++/WinRT is not available under MinGW/GCC) and Windows SDK 10.0.19041
// or newer; enable with -DEDGE_WITH_WINRT=ON in CMake, which is the default on
// MSVC.
//
// If you already own a Bluetooth connection — Qt, SimpleBLE, an in-house stack,
// or the BLE code NeuroGuide already ships — use edge::CallbackTransport
// instead and keep only the SDK's protocol and streaming logic.
//
// Threading: every method blocks on the underlying WinRT async operation, so
// call them from an MTA thread (a worker thread, not a UI/STA thread). The SDK
// serializes writes internally, so no external locking is needed.

#ifndef EDGE_WINRT_TRANSPORT_HPP
#define EDGE_WINRT_TRANSPORT_HPP

#if !defined(_WIN32)
#error "edge/winrt_transport.hpp is Windows-only; use edge::CallbackTransport elsewhere."
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "edge/transport.hpp"

namespace edge {

/// BLE transport over the Windows Runtime.
class WinRtTransport : public ITransport {
public:
    WinRtTransport();
    ~WinRtTransport() override;

    WinRtTransport(const WinRtTransport&) = delete;
    WinRtTransport& operator=(const WinRtTransport&) = delete;

    /// Scan for devices advertising the exact name "Narbis_Edge".
    ///
    /// The Edge does not put its service UUID in the advertising payload, so
    /// name matching is the only reliable filter.
    ///
    /// @param timeout_ms scan duration
    /// @return discovered devices, strongest signal first (may be empty)
    static std::vector<ScanResult> scan(int timeout_ms = 5000);

    /// Scan for and connect to the glasses.
    ///
    /// If an address was set with set_address(), connects to it directly and
    /// skips the scan.
    ///
    /// @param timeout_ms overall budget for scan + connect
    /// @throws DeviceNotFoundError if nothing advertises as "Narbis_Edge" —
    ///         the radio powers down after 2 minutes idle, so the cure is a
    ///         magnet tap on the temple, then a retry
    /// @throws ConnectionError if the GATT connection or service discovery fails
    void connect(int timeout_ms) override;

    /// Disconnect and release the GATT session. Never throws.
    void disconnect() override;

    bool is_connected() const override;

    /// @throws ConnectionError if not connected, CommandError if the write fails
    void write(const std::uint8_t* data, std::size_t len, bool with_response) override;

    /// True when 0xFF01 advertises write-without-response (firmware >= 4.16.3).
    /// Detected from the characteristic's properties at connect time.
    bool supports_write_without_response() const override;

    /// Read Battery Level (0x2A19) from the Battery Service (0x180F).
    /// @return false when the unit exposes no 0x180F service
    bool read_battery_level(int& out_level) override;

    /// Pin the transport to one device, skipping the scan on connect().
    /// @param address "AA:BB:CC:DD:EE:FF" or a bare 12-digit hex string
    void set_address(const std::string& address);

    /// The connected device's address, or an empty string.
    std::string address() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace edge

#endif  // EDGE_WINRT_TRANSPORT_HPP
