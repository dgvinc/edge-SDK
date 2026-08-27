// SPDX-License-Identifier: MIT
//
// Windows BLE transport, built on Windows.Devices.Bluetooth.
//
// Compiled only when the EDGE_WINRT constant is defined: automatically for the
// net8.0-windows10.0.19041.0 target, and for net472 when the project is built
// with -p:EdgeEnableWinRt=true (which adds Microsoft.Windows.SDK.Contracts).

#if EDGE_WINRT

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;

namespace Narbis.EdgeGlasses
{
    /// <summary>
    /// BLE transport over the Windows Runtime — the default on Windows.
    /// </summary>
    /// <remarks>
    /// <para>
    /// No pairing or bonding is required: the Edge uses no encryption. Windows
    /// does not need the device to be paired in Settings for a GATT connection.
    /// </para>
    /// <para>
    /// If your application already owns a Bluetooth connection, use
    /// <see cref="CallbackTransport"/> instead and keep only the SDK's protocol
    /// and streaming logic.
    /// </para>
    /// </remarks>
    public sealed class WinRtBleTransport : IGlassesTransport
    {
        private readonly object _gate = new object();

        private BluetoothLEDevice _device;
        private GattCharacteristic _control;
        private GattCharacteristic _batteryLevel;
        private bool _supportsWriteWithoutResponse;
        private string _address;
        private ulong? _pinnedAddress;
        private bool _disposed;

        /// <summary>Connect to whichever <c>Narbis_Edge</c> the scan finds first.</summary>
        public WinRtBleTransport()
        {
        }

        /// <summary>
        /// Connect to one specific device, skipping the scan.
        /// </summary>
        /// <param name="address">
        /// <c>AA:BB:CC:DD:EE:FF</c>, <c>AA-BB-...</c>, or a bare 12-digit hex string.
        /// </param>
        /// <exception cref="ArgumentException">The address could not be parsed.</exception>
        public WinRtBleTransport(string address)
        {
            if (!TryParseAddress(address, out ulong value))
            {
                throw new ArgumentException(
                    "Not a valid BLE address: '" + address + "' (expected AA:BB:CC:DD:EE:FF)",
                    nameof(address));
            }

            _pinnedAddress = value;
            _address = FormatAddress(value);
        }

        /// <inheritdoc />
        public bool IsConnected
        {
            get
            {
                lock (_gate)
                {
                    return _device != null && _control != null &&
                           _device.ConnectionStatus == BluetoothConnectionStatus.Connected;
                }
            }
        }

        /// <inheritdoc />
        public bool SupportsWriteWithoutResponse
        {
            get
            {
                lock (_gate)
                {
                    return _supportsWriteWithoutResponse;
                }
            }
        }

        /// <summary>The connected device's address, or null before connecting.</summary>
        public string Address
        {
            get
            {
                lock (_gate)
                {
                    return _address;
                }
            }
        }

        // -------------------------------------------------------------------
        // Scanning
        // -------------------------------------------------------------------

        /// <summary>
        /// Scan for devices advertising the exact name <c>Narbis_Edge</c>.
        /// </summary>
        /// <remarks>
        /// The Edge does not put its service UUID in the advertising payload, so
        /// name matching is the only reliable filter. Do not scan-filter on the
        /// 0x00FF service UUID: the earclip exposes it too.
        /// </remarks>
        /// <param name="timeout">Scan duration.</param>
        /// <param name="cancellationToken">Cancels the scan early.</param>
        /// <returns>Discovered devices, strongest signal first. May be empty.</returns>
        public static async Task<IReadOnlyList<ScanResult>> ScanAsync(
            TimeSpan? timeout = null,
            CancellationToken cancellationToken = default)
        {
            TimeSpan window = timeout ?? TimeSpan.FromSeconds(5);
            var found = new Dictionary<ulong, ScanResult>();
            var gate = new object();

            var watcher = new BluetoothLEAdvertisementWatcher
            {
                // Active scanning also collects scan-response payloads, where the
                // complete local name often lives.
                ScanningMode = BluetoothLEScanningMode.Active
            };

            void OnReceived(BluetoothLEAdvertisementWatcher sender,
                BluetoothLEAdvertisementReceivedEventArgs args)
            {
                if (!string.Equals(args.Advertisement.LocalName, Protocol.DeviceName,
                        StringComparison.Ordinal))
                {
                    return;
                }

                lock (gate)
                {
                    found[args.BluetoothAddress] = new ScanResult(
                        Protocol.DeviceName,
                        FormatAddress(args.BluetoothAddress),
                        args.RawSignalStrengthInDBm);
                }
            }

            watcher.Received += OnReceived;
            watcher.Start();
            try
            {
                await Task.Delay(window, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                // Return whatever was seen before cancellation.
            }
            finally
            {
                watcher.Received -= OnReceived;
                watcher.Stop();
            }

            lock (gate)
            {
                return found.Values.OrderByDescending(r => r.Rssi).ToList();
            }
        }

        // -------------------------------------------------------------------
        // Connection
        // -------------------------------------------------------------------

        /// <inheritdoc />
        public async Task ConnectAsync(TimeSpan timeout, CancellationToken cancellationToken)
        {
            ThrowIfDisposed();
            await DisconnectAsync().ConfigureAwait(false);

            ulong address;
            lock (_gate)
            {
                address = _pinnedAddress ?? 0UL;
            }

            if (address == 0UL)
            {
                // Spend at most half the budget looking for the device, leaving
                // the rest for the GATT connection itself.
                TimeSpan scanWindow = timeout > TimeSpan.Zero
                    ? TimeSpan.FromMilliseconds(timeout.TotalMilliseconds / 2)
                    : TimeSpan.FromSeconds(5);

                IReadOnlyList<ScanResult> devices =
                    await ScanAsync(scanWindow, cancellationToken).ConfigureAwait(false);

                if (devices.Count == 0)
                {
                    throw new DeviceNotFoundException(
                        "No EDGE Glasses ('" + Protocol.DeviceName + "') found. The glasses stop " +
                        "advertising 2 minutes after the last connection - tap the magnet to " +
                        "wake them, then retry.");
                }

                if (!TryParseAddress(devices[0].Address, out address))
                {
                    throw new GlassesConnectionException(
                        "Scan returned an unparseable address: " + devices[0].Address);
                }
            }

            BluetoothLEDevice device;
            try
            {
                device = await BluetoothLEDevice.FromBluetoothAddressAsync(address)
                    .AsTask(cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception ex)
            {
                throw new GlassesConnectionException("Failed to open the device: " + ex.Message, ex);
            }

            if (device == null)
            {
                throw new GlassesConnectionException(
                    "Failed to connect. If the glasses have been idle for over 2 minutes their " +
                    "radio is powered down - tap the magnet to wake them.");
            }

            try
            {
                // Uncached forces fresh GATT discovery: a table cached from an
                // earlier session can be stale after a firmware update.
                GattDeviceServicesResult services =
                    await device.GetGattServicesForUuidAsync(Protocol.ServiceUuid,
                        BluetoothCacheMode.Uncached).AsTask(cancellationToken)
                        .ConfigureAwait(false);

                if (services.Status != GattCommunicationStatus.Success ||
                    services.Services.Count == 0)
                {
                    throw new GlassesConnectionException(
                        "Control service 0x00FF not found on the device (status " +
                        services.Status + ").");
                }

                GattCharacteristicsResult characteristics =
                    await services.Services[0].GetCharacteristicsForUuidAsync(Protocol.ControlUuid,
                        BluetoothCacheMode.Uncached).AsTask(cancellationToken)
                        .ConfigureAwait(false);

                if (characteristics.Status != GattCommunicationStatus.Success ||
                    characteristics.Characteristics.Count == 0)
                {
                    throw new GlassesConnectionException(
                        "Control characteristic 0xFF01 not found on the device (status " +
                        characteristics.Status + ").");
                }

                GattCharacteristic control = characteristics.Characteristics[0];

                // Feature-detect write-without-response (firmware 4.16.3+). When
                // present the streaming path skips per-write ATT acks.
                bool fastWrite = control.CharacteristicProperties
                    .HasFlag(GattCharacteristicProperties.WriteWithoutResponse);

                GattCharacteristic battery =
                    await TryGetBatteryCharacteristicAsync(device, cancellationToken)
                        .ConfigureAwait(false);

                lock (_gate)
                {
                    _device = device;
                    _control = control;
                    _batteryLevel = battery;
                    _supportsWriteWithoutResponse = fastWrite;
                    _address = FormatAddress(address);
                }

                device = null;   // ownership transferred; do not dispose below
            }
            finally
            {
                device?.Dispose();
            }
        }

        /// <summary>
        /// Locate Battery Level (0x2A19). Optional: absent on pre-4.16.1 firmware
        /// and on boards built without the sense divider.
        /// </summary>
        private static async Task<GattCharacteristic> TryGetBatteryCharacteristicAsync(
            BluetoothLEDevice device, CancellationToken cancellationToken)
        {
            try
            {
                GattDeviceServicesResult services =
                    await device.GetGattServicesForUuidAsync(Protocol.BatteryServiceUuid,
                        BluetoothCacheMode.Uncached).AsTask(cancellationToken)
                        .ConfigureAwait(false);

                if (services.Status != GattCommunicationStatus.Success ||
                    services.Services.Count == 0)
                {
                    return null;
                }

                GattCharacteristicsResult characteristics =
                    await services.Services[0]
                        .GetCharacteristicsForUuidAsync(Protocol.BatteryLevelUuid,
                            BluetoothCacheMode.Uncached).AsTask(cancellationToken)
                        .ConfigureAwait(false);

                if (characteristics.Status != GattCommunicationStatus.Success ||
                    characteristics.Characteristics.Count == 0)
                {
                    return null;
                }

                return characteristics.Characteristics[0];
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception)
            {
                // No battery service on this unit; GetBatteryAsync reports null.
                return null;
            }
        }

        /// <inheritdoc />
        public Task DisconnectAsync()
        {
            BluetoothLEDevice device;
            lock (_gate)
            {
                device = _device;
                _device = null;
                _control = null;
                _batteryLevel = null;
                _supportsWriteWithoutResponse = false;
            }

            try
            {
                device?.Dispose();
            }
            catch (Exception)
            {
                // Already gone; nothing useful to do while tearing down.
            }

            return Task.FromResult(true);
        }

        // -------------------------------------------------------------------
        // I/O
        // -------------------------------------------------------------------

        /// <inheritdoc />
        public async Task WriteAsync(byte[] data, bool withResponse,
            CancellationToken cancellationToken)
        {
            ThrowIfDisposed();

            GattCharacteristic control;
            lock (_gate)
            {
                control = _control;
            }

            if (control == null)
            {
                throw new GlassesConnectionException("Not connected. Call ConnectAsync() first.");
            }

            var writer = new DataWriter();
            writer.WriteBytes(data);

            GattWriteOption option = withResponse
                ? GattWriteOption.WriteWithResponse
                : GattWriteOption.WriteWithoutResponse;

            try
            {
                GattWriteResult result = await control
                    .WriteValueWithResultAsync(writer.DetachBuffer(), option)
                    .AsTask(cancellationToken).ConfigureAwait(false);

                if (result.Status != GattCommunicationStatus.Success)
                {
                    throw new CommandException(
                        "Command failed: GATT write returned " + result.Status + ".");
                }
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (CommandException)
            {
                throw;
            }
            catch (Exception ex)
            {
                throw new CommandException("Command failed: " + ex.Message, ex);
            }
        }

        /// <inheritdoc />
        public async Task<int?> ReadBatteryLevelAsync(CancellationToken cancellationToken)
        {
            ThrowIfDisposed();

            GattCharacteristic battery;
            lock (_gate)
            {
                battery = _batteryLevel;
            }

            if (battery == null)
            {
                return null;   // pre-4.16.1 firmware, or no battery service
            }

            try
            {
                GattReadResult result = await battery
                    .ReadValueAsync(BluetoothCacheMode.Uncached)
                    .AsTask(cancellationToken).ConfigureAwait(false);

                if (result.Status != GattCommunicationStatus.Success)
                {
                    throw new CommandException(
                        "Battery read failed: " + result.Status + ".");
                }

                if (result.Value == null || result.Value.Length == 0)
                {
                    return null;
                }

                DataReader reader = DataReader.FromBuffer(result.Value);
                return reader.ReadByte();
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (CommandException)
            {
                throw;
            }
            catch (Exception ex)
            {
                throw new CommandException("Battery read failed: " + ex.Message, ex);
            }
        }

        // -------------------------------------------------------------------

        /// <summary>Format a 48-bit BLE address as <c>AA:BB:CC:DD:EE:FF</c>.</summary>
        internal static string FormatAddress(ulong address)
        {
            return string.Join(":", Enumerable.Range(0, 6)
                .Select(i => ((address >> ((5 - i) * 8)) & 0xFF)
                    .ToString("X2", CultureInfo.InvariantCulture)));
        }

        /// <summary>Parse <c>AA:BB:CC:DD:EE:FF</c>, <c>AA-BB-...</c>, or bare hex.</summary>
        internal static bool TryParseAddress(string text, out ulong address)
        {
            address = 0;
            if (string.IsNullOrWhiteSpace(text))
            {
                return false;
            }

            string hex = text.Replace(":", string.Empty)
                             .Replace("-", string.Empty)
                             .Replace(" ", string.Empty);

            return hex.Length == 6 * 2 &&
                   ulong.TryParse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture,
                       out address);
        }

        private void ThrowIfDisposed()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(WinRtBleTransport));
            }
        }

        /// <summary>Disconnect and release the underlying device.</summary>
        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
            DisconnectAsync().GetAwaiter().GetResult();
        }
    }
}

#endif
