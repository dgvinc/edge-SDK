// SPDX-License-Identifier: MIT
//
// Shared example plumbing: hand back a transport for whatever this build has.
//
// On Windows/MSVC (EDGE_WITH_WINRT) that is the built-in WinRT Bluetooth
// transport. Anywhere else there is no bundled BLE stack, so the examples say
// so and exit rather than pretending to connect — in a real integration you
// would return an edge::CallbackTransport wrapping your own stack here.

#ifndef EDGE_EXAMPLES_CONNECT_HELPER_HPP
#define EDGE_EXAMPLES_CONNECT_HELPER_HPP

#include <iostream>
#include <memory>

#include "edge/transport.hpp"

#if defined(EDGE_WITH_WINRT)
#include "edge/winrt_transport.hpp"
#endif

namespace example {

/// @return a transport, or nullptr if this build has no bundled BLE stack.
inline std::unique_ptr<edge::ITransport> make_transport() {
#if defined(EDGE_WITH_WINRT)
    return std::unique_ptr<edge::ITransport>(new edge::WinRtTransport());
#else
    std::cout << "This build has no bundled BLE transport.\n"
                 "  * On Windows, configure with MSVC (-DEDGE_WITH_WINRT=ON) for the\n"
                 "    built-in WinRT transport.\n"
                 "  * Otherwise wrap your own Bluetooth stack in an\n"
                 "    edge::CallbackTransport - see the README.\n";
    return nullptr;
#endif
}

/// Print the standard "device not found" guidance.
inline void explain_not_found() {
    std::cout << "\nCould not find the glasses.\n"
                 "The radio powers down after 2 minutes with no client connected -\n"
                 "tap the magnet to the temple to wake them, then run this again.\n";
}

}  // namespace example

#endif  // EDGE_EXAMPLES_CONNECT_HELPER_HPP
