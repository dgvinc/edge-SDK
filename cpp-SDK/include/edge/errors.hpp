// SPDX-License-Identifier: MIT
//
// EDGE Glasses — exception types.
//
// Mirrors the Python SDK's exception hierarchy (edge_glasses.exceptions) so the
// error contract is the same in every language binding.

#ifndef EDGE_ERRORS_HPP
#define EDGE_ERRORS_HPP

#include <stdexcept>
#include <string>

namespace edge {

/// Base class for every error this SDK raises.
class GlassesError : public std::runtime_error {
public:
    explicit GlassesError(const std::string& what) : std::runtime_error(what) {}
};

/// Not connected, or the connection attempt failed.
class ConnectionError : public GlassesError {
public:
    explicit ConnectionError(const std::string& what) : GlassesError(what) {}
};

/// No device advertising as "Narbis_Edge" was found.
///
/// The glasses power their radio down after 2 minutes with no client connected —
/// the usual cure is a magnet tap on the temple, then a rescan.
class DeviceNotFoundError : public GlassesError {
public:
    explicit DeviceNotFoundError(const std::string& what) : GlassesError(what) {}
};

/// A GATT write or read failed at the transport layer.
///
/// Note this never means "the firmware rejected the argument": the firmware
/// never NACKs a command. It means the write did not reach the device.
class CommandError : public GlassesError {
public:
    explicit CommandError(const std::string& what) : GlassesError(what) {}
};

/// An operation exceeded its timeout.
class TimeoutError : public GlassesError {
public:
    explicit TimeoutError(const std::string& what) : GlassesError(what) {}
};

}  // namespace edge

#endif  // EDGE_ERRORS_HPP
