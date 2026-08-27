// SPDX-License-Identifier: MIT

using System;
using System.Threading;
using System.Threading.Tasks;

namespace Narbis.EdgeGlasses
{
    /// <summary>
    /// Adapts an existing GATT stack to <see cref="IGlassesTransport"/>.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Use this when your application already owns the BLE connection — the
    /// Bluetooth code an existing product already ships, a cross-platform stack,
    /// or a test double — and you want only the SDK's protocol encoding,
    /// clamping and streaming logic.
    /// </para>
    /// <code>
    /// var transport = new CallbackTransport(
    ///     writeAsync: (frame, withResponse, ct) => MyGattWriteAsync(frame, withResponse, ct),
    ///     isConnected: () => myLink.IsUp);
    /// var glasses = new Glasses(transport);
    /// await glasses.SetStaticAsync(50);
    /// </code>
    /// </remarks>
    public sealed class CallbackTransport : IGlassesTransport
    {
        private readonly Func<byte[], bool, CancellationToken, Task> _writeAsync;
        private readonly Func<bool> _isConnected;
        private Func<CancellationToken, Task<int?>> _readBatteryAsync;

        /// <summary>
        /// Create a transport backed by your own Bluetooth stack.
        /// </summary>
        /// <param name="writeAsync">
        /// Writes a frame to characteristic 0xFF01. Throw to signal failure; the
        /// SDK surfaces it as <see cref="CommandException"/>.
        /// </param>
        /// <param name="isConnected">Reports whether the link is up.</param>
        public CallbackTransport(
            Func<byte[], bool, CancellationToken, Task> writeAsync,
            Func<bool> isConnected)
        {
            _writeAsync = writeAsync ?? throw new ArgumentNullException(nameof(writeAsync));
            _isConnected = isConnected ?? throw new ArgumentNullException(nameof(isConnected));
        }

        /// <inheritdoc />
        public bool IsConnected
        {
            get { return _isConnected(); }
        }

        /// <inheritdoc />
        /// <remarks>
        /// Off by default, which is always safe. Set it to true once you have
        /// checked that 0xFF01 advertises write-without-response (firmware
        /// 4.16.3+), and the streaming path will skip per-write ATT acks.
        /// </remarks>
        public bool SupportsWriteWithoutResponse { get; set; }

        /// <summary>
        /// Supply a battery reader. Without one,
        /// <see cref="Glasses.GetBatteryAsync"/> reports "not available".
        /// </summary>
        public void SetBatteryReader(Func<CancellationToken, Task<int?>> readBatteryAsync)
        {
            _readBatteryAsync = readBatteryAsync;
        }

        /// <inheritdoc />
        /// <remarks>A no-op: the caller owns this connection's lifecycle.</remarks>
        public Task ConnectAsync(TimeSpan timeout, CancellationToken cancellationToken)
        {
            return Task.FromResult(true);
        }

        /// <inheritdoc />
        /// <remarks>A no-op: the caller owns this connection's lifecycle.</remarks>
        public Task DisconnectAsync()
        {
            return Task.FromResult(true);
        }

        /// <inheritdoc />
        public Task WriteAsync(byte[] data, bool withResponse, CancellationToken cancellationToken)
        {
            return _writeAsync(data, withResponse, cancellationToken);
        }

        /// <inheritdoc />
        public Task<int?> ReadBatteryLevelAsync(CancellationToken cancellationToken)
        {
            if (_readBatteryAsync == null)
            {
                return Task.FromResult<int?>(null);
            }

            return _readBatteryAsync(cancellationToken);
        }

        /// <inheritdoc />
        /// <remarks>Nothing to release: this transport owns no resources.</remarks>
        public void Dispose()
        {
        }
    }
}
