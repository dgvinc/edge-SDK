// SPDX-License-Identifier: MIT

using System;

namespace Narbis.EdgeGlasses
{
    /// <summary>
    /// Base class for every error this SDK raises. Mirrors the Python SDK's
    /// exception hierarchy so the error contract is the same in every binding.
    /// </summary>
    public class GlassesException : Exception
    {
        /// <summary>Create the exception.</summary>
        public GlassesException(string message) : base(message)
        {
        }

        /// <summary>Create the exception, wrapping the underlying cause.</summary>
        public GlassesException(string message, Exception innerException)
            : base(message, innerException)
        {
        }
    }

    /// <summary>Not connected, or the connection attempt failed.</summary>
    public class GlassesConnectionException : GlassesException
    {
        /// <summary>Create the exception.</summary>
        public GlassesConnectionException(string message) : base(message)
        {
        }

        /// <summary>Create the exception, wrapping the underlying cause.</summary>
        public GlassesConnectionException(string message, Exception innerException)
            : base(message, innerException)
        {
        }
    }

    /// <summary>
    /// No device advertising as <c>Narbis_Edge</c> was found.
    /// </summary>
    /// <remarks>
    /// The glasses power their radio down after 2 minutes with no client
    /// connected. The cure is a magnet tap on the temple, then a rescan —
    /// surface that in your UX rather than retrying silently.
    /// </remarks>
    public class DeviceNotFoundException : GlassesException
    {
        /// <summary>Create the exception.</summary>
        public DeviceNotFoundException(string message) : base(message)
        {
        }

        /// <summary>Create the exception, wrapping the underlying cause.</summary>
        public DeviceNotFoundException(string message, Exception innerException)
            : base(message, innerException)
        {
        }
    }

    /// <summary>
    /// A GATT write or read failed at the transport layer.
    /// </summary>
    /// <remarks>
    /// This never means "the firmware rejected the argument" — the firmware
    /// never NACKs a command. It means the write did not reach the device.
    /// </remarks>
    public class CommandException : GlassesException
    {
        /// <summary>Create the exception.</summary>
        public CommandException(string message) : base(message)
        {
        }

        /// <summary>Create the exception, wrapping the underlying cause.</summary>
        public CommandException(string message, Exception innerException)
            : base(message, innerException)
        {
        }
    }

    /// <summary>An operation exceeded its timeout.</summary>
    public class GlassesTimeoutException : GlassesException
    {
        /// <summary>Create the exception.</summary>
        public GlassesTimeoutException(string message) : base(message)
        {
        }

        /// <summary>Create the exception, wrapping the underlying cause.</summary>
        public GlassesTimeoutException(string message, Exception innerException)
            : base(message, innerException)
        {
        }
    }
}
