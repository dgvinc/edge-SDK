// SPDX-License-Identifier: MIT
//
// Connect and drive the lens — the copy-paste minimum.
//
// Wake the glasses with a magnet tap first, then run this. It connects, sets a
// session guard so the auto-sleep timer will not end things early, and steps
// the lens through clear -> half -> dark -> clear.

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

        // The glasses deep-sleep when the session timer expires, and the timer
        // runs from device wake - not from this write. Set it >= your session.
        glasses.set_duration(60);

        if (glasses.supports_fast_write()) {
            std::cout << "Firmware >= 4.16.3: write-without-response available.\n";
        }

        const auto battery = glasses.get_battery();
        if (battery.has_value()) {
            std::cout << "Battery: " << *battery << " %\n";
        } else {
            std::cout << "Battery: not reported by this unit.\n";
        }

        auto hold = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };

        std::cout << "clear\n";
        glasses.set_static(0);
        hold(1500);

        std::cout << "half tint\n";
        glasses.set_static(50);
        hold(1500);

        std::cout << "fully dark\n";
        glasses.set_static(100);
        hold(1500);

        // Always leave the wearer able to see: without this the lens FREEZES at
        // its last tint across the disconnect.
        std::cout << "clear, disconnecting\n";
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
