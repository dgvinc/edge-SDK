// SPDX-License-Identifier: MIT

using System;
using System.Threading;
using System.Threading.Tasks;

namespace Narbis.EdgeGlasses
{
    /// <summary>A device discovered by a scan.</summary>
    public sealed class ScanResult
    {
        /// <summary>Create a scan result.</summary>
        public ScanResult(string name, string address, int rssi)
        {
            Name = name;
            Address = address;
            Rssi = rssi;
        }

        /// <summary>Advertised name; always <c>Narbis_Edge</c>.</summary>
        public string Name { get; }

        /// <summary>Device address, formatted <c>AA:BB:CC:DD:EE:FF</c>.</summary>
        public string Address { get; }

        /// <summary>Signal strength in dBm.</summary>
        public int Rssi { get; }

        /// <inheritdoc />
        public override string ToString()
        {
            return Name + " (" + Address + ") RSSI: " + Rssi.ToString(
                System.Globalization.CultureInfo.InvariantCulture);
        }
    }

    /// <summary>
    /// Abstract BLE link to one pair of glasses.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <see cref="Glasses"/> talks to the device only through this interface, so
    /// the SDK is not tied to any one Bluetooth stack. Two implementations ship
    /// with it: <c>WinRtBleTransport</c> for Windows (Windows targets only), and
    /// <see cref="CallbackTransport"/> to adapt a stack you already own.
    /// </para>
    /// <para>
    /// The SDK guarantees it never issues overlapping writes, so an
    /// implementation does not need to be re-entrant.
    /// </para>
    /// </remarks>
    public interface IGlassesTransport : IDisposable
    {
        /// <summary>True while a GATT connection is established.</summary>
        bool IsConnected { get; }

        /// <summary>
        /// True if 0xFF01 advertises write-without-response (firmware 4.16.3+).
        /// </summary>
        /// <remarks>
        /// When true the streaming path skips the per-write ATT round-trip,
        /// lifting sustained throughput past the ~20 writes/sec that acked
        /// writes allow. Older firmware is write-with-response only.
        /// </remarks>
        bool SupportsWriteWithoutResponse { get; }

        /// <summary>
        /// Establish the link, for transports that own their connection.
        /// Implementations wrapping a link the caller already opened may no-op.
        /// </summary>
        /// <exception cref="DeviceNotFoundException">Nothing advertises as <c>Narbis_Edge</c>.</exception>
        /// <exception cref="GlassesConnectionException">The connection failed.</exception>
        Task ConnectAsync(TimeSpan timeout, CancellationToken cancellationToken);

        /// <summary>Tear down the link. Never throws.</summary>
        Task DisconnectAsync();

        /// <summary>
        /// Write bytes to the control characteristic 0xFF01.
        /// </summary>
        /// <param name="data">
        /// Frame bytes, already clamped and padded by the caller. Length is 1
        /// only for the legacy opacity write.
        /// </param>
        /// <param name="withResponse">
        /// true for write-with-response (ordering and back-pressure). The SDK
        /// passes false only on the high-rate streaming path, and only when
        /// <see cref="SupportsWriteWithoutResponse"/> is true.
        /// </param>
        /// <param name="cancellationToken">Cancels the write.</param>
        /// <exception cref="CommandException">The write did not reach the device.</exception>
        Task WriteAsync(byte[] data, bool withResponse, CancellationToken cancellationToken);

        /// <summary>
        /// Read Battery Level (0x2A19) from the Battery Service (0x180F).
        /// </summary>
        /// <returns>
        /// Charge 0-100, or null when the device exposes no 0x180F service
        /// (pre-4.16.1 firmware, or a board built without the battery divider).
        /// </returns>
        /// <exception cref="CommandException">The service exists but the read failed.</exception>
        Task<int?> ReadBatteryLevelAsync(CancellationToken cancellationToken);
    }
}
