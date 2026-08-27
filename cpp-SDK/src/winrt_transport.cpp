// SPDX-License-Identifier: MIT
//
// Windows BLE transport for the EDGE Glasses SDK (C++/WinRT).
//
// Compiled only when EDGE_WITH_WINRT is defined (CMake turns it on by default
// under MSVC). Requires the Windows SDK's C++/WinRT projection headers.

#if defined(EDGE_WITH_WINRT)

#include "edge/winrt_transport.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#include "edge/protocol.hpp"

namespace edge {
namespace {

namespace wdb = winrt::Windows::Devices::Bluetooth;
namespace wdba = winrt::Windows::Devices::Bluetooth::Advertisement;
namespace wdbg = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
namespace wss = winrt::Windows::Storage::Streams;

/// Format a 48-bit BLE address the way bleak and the Windows UI show it.
std::string format_address(std::uint64_t address) {
    char buf[18];
    std::snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X",
                  static_cast<unsigned>((address >> 40) & 0xFF),
                  static_cast<unsigned>((address >> 32) & 0xFF),
                  static_cast<unsigned>((address >> 24) & 0xFF),
                  static_cast<unsigned>((address >> 16) & 0xFF),
                  static_cast<unsigned>((address >> 8) & 0xFF),
                  static_cast<unsigned>(address & 0xFF));
    return std::string(buf);
}

/// Parse "AA:BB:CC:DD:EE:FF", "AA-BB-...", or a bare 12-hex-digit string.
bool parse_address(const std::string& text, std::uint64_t& out) {
    std::string hex;
    for (char c : text) {
        if (c == ':' || c == '-' || c == ' ') continue;
        hex.push_back(c);
    }
    if (hex.size() != 12) return false;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(hex.c_str(), &end, 16);
    if (end == nullptr || *end != '\0') return false;
    out = static_cast<std::uint64_t>(value);
    return true;
}

/// Ensure this thread is in a COM apartment. The blocking .get() calls below
/// require MTA; if the caller already initialised an apartment we keep theirs.
void ensure_apartment() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error&) {
        // RPC_E_CHANGED_MODE: the thread is already in an apartment. That is
        // fine for STA callers as long as they do not block the UI thread.
    }
}



winrt::guid short_uuid(std::uint16_t id) {
    return wdb::BluetoothUuidHelper::FromShortId(id);
}

}  // namespace

struct WinRtTransport::Impl {
    std::mutex mutex;
    wdb::BluetoothLEDevice device{nullptr};
    wdbg::GattCharacteristic control{nullptr};
    wdbg::GattCharacteristic battery_level{nullptr};
    std::atomic<bool> connected{false};
    bool fast_write = false;
    std::string address_text;
    std::uint64_t address_value = 0;
    bool has_address = false;
};

WinRtTransport::WinRtTransport() : impl_(new Impl()) { ensure_apartment(); }

WinRtTransport::~WinRtTransport() { disconnect(); }

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

std::vector<ScanResult> WinRtTransport::scan(int timeout_ms) {
    ensure_apartment();

    // Shared, not captured by reference: the watcher delivers callbacks on a
    // background thread, and revoking the handler does not guarantee an
    // in-flight one has already returned. Owning the state through a shared_ptr
    // keeps it alive even if a late callback lands after this function returns.
    struct ScanState {
        std::mutex mutex;
        std::vector<ScanResult> found;
    };
    auto state = std::make_shared<ScanState>();

    wdba::BluetoothLEAdvertisementWatcher watcher;
    // Active scanning picks up scan-response payloads, where the complete local
    // name often lives.
    watcher.ScanningMode(wdba::BluetoothLEScanningMode::Active);

    const auto token = watcher.Received(
        [state](wdba::BluetoothLEAdvertisementWatcher const&,
                wdba::BluetoothLEAdvertisementReceivedEventArgs const& args) {
            const auto name = winrt::to_string(args.Advertisement().LocalName());
            if (name != kDeviceName) return;  // exact match: the service UUID is not advertised

            const std::string addr = format_address(args.BluetoothAddress());
            std::lock_guard<std::mutex> lock(state->mutex);
            for (auto& existing : state->found) {
                if (existing.address == addr) {
                    existing.rssi = args.RawSignalStrengthInDBm();
                    return;
                }
            }
            ScanResult r;
            r.name = name;
            r.address = addr;
            r.rssi = args.RawSignalStrengthInDBm();
            state->found.push_back(r);
        });

    watcher.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms));
    watcher.Stop();
    watcher.Received(token);

    std::lock_guard<std::mutex> lock(state->mutex);
    std::vector<ScanResult> found = state->found;
    std::sort(found.begin(), found.end(),
              [](const ScanResult& a, const ScanResult& b) { return a.rssi > b.rssi; });
    return found;
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void WinRtTransport::set_address(const std::string& address) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::uint64_t value = 0;
    if (!parse_address(address, value)) {
        throw ConnectionError("not a valid BLE address: '" + address +
                             "' (expected AA:BB:CC:DD:EE:FF)");
    }
    // Store the normalised form, so address() reads back consistently whether it
    // was set here or discovered by a scan (matches the C# transport).
    impl_->address_text = format_address(value);
    impl_->address_value = value;
    impl_->has_address = true;
}

std::string WinRtTransport::address() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->address_text;
}

void WinRtTransport::connect(int timeout_ms) {
    ensure_apartment();
    disconnect();

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);

    std::uint64_t address_value = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->has_address) address_value = impl_->address_value;
    }

    if (address_value == 0) {
        // Spend at most half the budget looking for the device, leaving the
        // rest for the GATT connection itself.
        const int scan_ms = timeout_ms > 0 ? (timeout_ms / 2) : 5000;
        const std::vector<ScanResult> devices = scan(scan_ms);
        if (devices.empty()) {
            throw DeviceNotFoundError(
                "No EDGE Glasses ('Narbis_Edge') found. The glasses stop advertising 2 minutes "
                "after the last connection - tap the magnet to wake them, then retry.");
        }
        if (!parse_address(devices.front().address, address_value)) {
            throw ConnectionError("scan returned an unparseable address: " +
                                 devices.front().address);
        }
    }

    wdb::BluetoothLEDevice device{nullptr};
    try {
        device = wdb::BluetoothLEDevice::FromBluetoothAddressAsync(address_value).get();
    } catch (const winrt::hresult_error& e) {
        throw ConnectionError("Failed to open the device: " + winrt::to_string(e.message()));
    }
    if (device == nullptr) {
        throw ConnectionError(
            "Failed to connect. If the glasses have been idle for over 2 minutes their radio is "
            "powered down - tap the magnet to wake them.");
    }

    if (std::chrono::steady_clock::now() > deadline) {
        throw TimeoutError("Connection timed out");
    }

    // Uncached forces a fresh GATT discovery: a cached table from a previous
    // session can be stale after a firmware update.
    wdbg::GattDeviceServicesResult services{nullptr};
    try {
        services = device.GetGattServicesForUuidAsync(short_uuid(uuid::kServiceShort),
                                                      wdb::BluetoothCacheMode::Uncached)
                       .get();
    } catch (const winrt::hresult_error& e) {
        throw ConnectionError("Service discovery failed: " + winrt::to_string(e.message()));
    }
    if (services == nullptr || services.Status() != wdbg::GattCommunicationStatus::Success ||
        services.Services().Size() == 0) {
        throw ConnectionError("Control service 0x00FF not found on the device");
    }

    const auto service = services.Services().GetAt(0);
    const auto characteristics =
        service.GetCharacteristicsForUuidAsync(short_uuid(uuid::kControlShort),
                                               wdb::BluetoothCacheMode::Uncached)
            .get();
    if (characteristics == nullptr ||
        characteristics.Status() != wdbg::GattCommunicationStatus::Success ||
        characteristics.Characteristics().Size() == 0) {
        throw ConnectionError("Control characteristic 0xFF01 not found on the device");
    }

    const auto control = characteristics.Characteristics().GetAt(0);

    // Feature-detect write-without-response (firmware >= 4.16.3). When present,
    // the streaming path skips per-write ATT acks for higher throughput.
    const bool fast_write =
        (control.CharacteristicProperties() &
         wdbg::GattCharacteristicProperties::WriteWithoutResponse) !=
        wdbg::GattCharacteristicProperties::None;

    // Battery Service is optional: absent on pre-4.16.1 firmware and on boards
    // built without the sense divider.
    wdbg::GattCharacteristic battery{nullptr};
    try {
        const auto battery_services =
            device.GetGattServicesForUuidAsync(short_uuid(uuid::kBatteryServiceShort),
                                               wdb::BluetoothCacheMode::Uncached)
                .get();
        if (battery_services != nullptr &&
            battery_services.Status() == wdbg::GattCommunicationStatus::Success &&
            battery_services.Services().Size() > 0) {
            const auto battery_chars =
                battery_services.Services()
                    .GetAt(0)
                    .GetCharacteristicsForUuidAsync(short_uuid(uuid::kBatteryLevelShort),
                                                    wdb::BluetoothCacheMode::Uncached)
                    .get();
            if (battery_chars != nullptr &&
                battery_chars.Status() == wdbg::GattCommunicationStatus::Success &&
                battery_chars.Characteristics().Size() > 0) {
                battery = battery_chars.Characteristics().GetAt(0);
            }
        }
    } catch (const winrt::hresult_error&) {
        // No battery service on this unit; get_battery() reports "unavailable".
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->device = device;
    impl_->control = control;
    impl_->battery_level = battery;
    impl_->fast_write = fast_write;
    impl_->address_text = format_address(address_value);
    impl_->address_value = address_value;
    impl_->connected = true;
}

void WinRtTransport::disconnect() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->connected = false;
    impl_->control = nullptr;
    impl_->battery_level = nullptr;
    if (impl_->device != nullptr) {
        try {
            impl_->device.Close();
        } catch (...) {
            // Already gone; nothing useful to do while tearing down.
        }
        impl_->device = nullptr;
    }
    impl_->fast_write = false;
}

bool WinRtTransport::is_connected() const {
    if (!impl_->connected.load()) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->device == nullptr) return false;
    return impl_->device.ConnectionStatus() == wdb::BluetoothConnectionStatus::Connected;
}

bool WinRtTransport::supports_write_without_response() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->fast_write;
}

// ---------------------------------------------------------------------------
// I/O
// ---------------------------------------------------------------------------

void WinRtTransport::write(const std::uint8_t* data, std::size_t len, bool with_response) {
    wdbg::GattCharacteristic control{nullptr};
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        control = impl_->control;
    }
    if (control == nullptr) {
        throw ConnectionError("Not connected. Call connect() first.");
    }

    wss::DataWriter writer;
    for (std::size_t i = 0; i < len; ++i) writer.WriteByte(data[i]);

    const auto option = with_response ? wdbg::GattWriteOption::WriteWithResponse
                                      : wdbg::GattWriteOption::WriteWithoutResponse;
    try {
        const auto result = control.WriteValueWithResultAsync(writer.DetachBuffer(), option).get();
        if (result == nullptr || result.Status() != wdbg::GattCommunicationStatus::Success) {
            throw CommandError("Command failed: GATT write did not complete successfully");
        }
    } catch (const winrt::hresult_error& e) {
        throw CommandError("Command failed: " + winrt::to_string(e.message()));
    }
}

bool WinRtTransport::read_battery_level(int& out_level) {
    wdbg::GattCharacteristic battery{nullptr};
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        battery = impl_->battery_level;
    }
    if (battery == nullptr) return false;  // pre-4.16.1 firmware / no battery service

    try {
        const auto result = battery.ReadValueAsync(wdb::BluetoothCacheMode::Uncached).get();
        if (result == nullptr || result.Status() != wdbg::GattCommunicationStatus::Success) {
            throw CommandError("Battery read failed");
        }
        const auto buffer = result.Value();
        if (buffer == nullptr || buffer.Length() == 0) return false;
        const auto reader = wss::DataReader::FromBuffer(buffer);
        out_level = static_cast<int>(reader.ReadByte());
        return true;
    } catch (const winrt::hresult_error& e) {
        throw CommandError("Battery read failed: " + winrt::to_string(e.message()));
    }
}

}  // namespace edge

#endif  // EDGE_WITH_WINRT
