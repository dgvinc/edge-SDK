// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Narbis.EdgeGlasses.Tests
{
    /// <summary>One recorded GATT write.</summary>
    public sealed class RecordedWrite
    {
        public RecordedWrite(byte[] bytes, bool withResponse)
        {
            Bytes = bytes;
            WithResponse = withResponse;
        }

        public byte[] Bytes { get; }

        public bool WithResponse { get; }
    }

    /// <summary>
    /// A recording transport: captures every frame the SDK puts on the wire so
    /// assertions can compare exact bytes against the protocol document.
    /// </summary>
    public sealed class MockTransport : IGlassesTransport
    {
        private readonly object _gate = new object();
        private readonly List<RecordedWrite> _writes = new List<RecordedWrite>();
        private int _failNext;

        public bool IsConnected { get; set; } = true;

        public bool SupportsWriteWithoutResponse { get; set; }

        public int? BatteryLevel { get; set; }

        public int ConnectCalls { get; private set; }

        public int DisconnectCalls { get; private set; }

        public int FailedWrites { get; private set; }

        public bool Disposed { get; private set; }

        /// <summary>Make the next <paramref name="count"/> writes throw.</summary>
        public void FailNextWrites(int count)
        {
            lock (_gate)
            {
                _failNext = count;
            }
        }

        /// <summary>Copy the recorded writes without draining them.</summary>
        public IReadOnlyList<RecordedWrite> Snapshot()
        {
            lock (_gate)
            {
                return _writes.ToArray();
            }
        }

        /// <summary>Drain the recorded writes.</summary>
        public IReadOnlyList<RecordedWrite> TakeWrites()
        {
            lock (_gate)
            {
                RecordedWrite[] copy = _writes.ToArray();
                _writes.Clear();
                return copy;
            }
        }

        public int WriteCount
        {
            get
            {
                lock (_gate)
                {
                    return _writes.Count;
                }
            }
        }

        public Task ConnectAsync(TimeSpan timeout, CancellationToken cancellationToken)
        {
            ConnectCalls++;
            IsConnected = true;
            return Task.CompletedTask;
        }

        public Task DisconnectAsync()
        {
            DisconnectCalls++;
            IsConnected = false;
            return Task.CompletedTask;
        }

        public Task WriteAsync(byte[] data, bool withResponse, CancellationToken cancellationToken)
        {
            lock (_gate)
            {
                if (_failNext > 0)
                {
                    _failNext--;
                    FailedWrites++;
                    throw new CommandException("mock: injected write failure");
                }

                _writes.Add(new RecordedWrite((byte[])data.Clone(), withResponse));
            }

            return Task.CompletedTask;
        }

        public Task<int?> ReadBatteryLevelAsync(CancellationToken cancellationToken)
        {
            return Task.FromResult(BatteryLevel);
        }

        public void Dispose()
        {
            Disposed = true;
        }
    }
}
