// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Xunit;

namespace Narbis.EdgeGlasses.Tests
{
    public class GlassesTests
    {
        private static (MockTransport, Glasses) Create()
        {
            var transport = new MockTransport();
            return (transport, new Glasses(transport));
        }

        // -------------------------------------------------------------------
        // Single commands
        // -------------------------------------------------------------------

        [Fact]
        public async Task SetOpacity_WritesExactlyOneByteWithResponse()
        {
            (MockTransport t, Glasses g) = Create();
            await g.SetOpacityAsync(128);

            RecordedWrite write = Assert.Single(t.Snapshot());
            Assert.Equal(new byte[] { 0x80 }, write.Bytes);
            Assert.True(write.WithResponse);
        }

        [Fact]
        public async Task ClearAndDark_UseTheLegacyOpacityWrite()
        {
            (MockTransport t, Glasses g) = Create();
            await g.ClearAsync();
            await g.DarkAsync();

            IReadOnlyList<RecordedWrite> writes = t.Snapshot();
            Assert.Equal(new byte[] { 0x00 }, writes[0].Bytes);
            Assert.Equal(new byte[] { 0xFF }, writes[1].Bytes);
        }

        [Fact]
        public async Task CommandWrites_UseWriteWithResponse()
        {
            (MockTransport t, Glasses g) = Create();
            await g.SetStaticAsync(50);

            RecordedWrite write = Assert.Single(t.Snapshot());
            Assert.Equal(new byte[] { 0xA5, 50 }, write.Bytes);
            Assert.True(write.WithResponse);
        }

        [Fact]
        public async Task SendCommand_PadsAndForwards()
        {
            (MockTransport t, Glasses g) = Create();
            await g.SendCommandAsync(0xA7);
            await g.SendCommandAsync(0xA2, 80);

            IReadOnlyList<RecordedWrite> writes = t.Snapshot();
            Assert.Equal(new byte[] { 0xA7, 0x00 }, writes[0].Bytes);
            Assert.Equal(new byte[] { 0xA2, 80 }, writes[1].Bytes);
        }

        // -------------------------------------------------------------------
        // Multi-write sequences
        // -------------------------------------------------------------------

        [Fact]
        public async Task StartStrobe_WithoutParameters_WritesOnlyTheModeByte()
        {
            (MockTransport t, Glasses g) = Create();
            await g.StartStrobeAsync();

            RecordedWrite write = Assert.Single(t.Snapshot());
            Assert.Equal(new byte[] { 0xA6, 0x00 }, write.Bytes);
        }

        [Fact]
        public async Task StartStrobe_WritesFrequencyThenDutyThenMode()
        {
            (MockTransport t, Glasses g) = Create();
            await g.StartStrobeAsync(10, 50);

            IReadOnlyList<RecordedWrite> writes = t.Snapshot();
            Assert.Equal(3, writes.Count);
            Assert.Equal(new byte[] { 0xAB, 10 }, writes[0].Bytes);
            Assert.Equal(new byte[] { 0xAC, 50 }, writes[1].Bytes);
            Assert.Equal(new byte[] { 0xA6, 0x00 }, writes[2].Bytes);
        }

        [Fact]
        public async Task StartStrobe_WritesOnlyTheParametersSupplied()
        {
            (MockTransport t, Glasses g) = Create();
            await g.StartStrobeAsync(dutyPct: 40);

            IReadOnlyList<RecordedWrite> writes = t.Snapshot();
            Assert.Equal(2, writes.Count);
            Assert.Equal(new byte[] { 0xAC, 40 }, writes[0].Bytes);
            Assert.Equal(new byte[] { 0xA6, 0x00 }, writes[1].Bytes);
        }

        [Fact]
        public async Task StartBreathe_WithoutOptions_WritesOnlyTheModeByte()
        {
            (MockTransport t, Glasses g) = Create();
            await g.StartBreatheAsync();

            RecordedWrite write = Assert.Single(t.Snapshot());
            Assert.Equal(new byte[] { 0xB0, 0x00 }, write.Bytes);
        }

        [Fact]
        public async Task StartBreathe_WritesParametersInOrderThenTheMode()
        {
            (MockTransport t, Glasses g) = Create();
            await g.StartBreatheAsync(new BreatheOptions
            {
                Bpm = 5,
                InhalePct = 40,
                HoldTopMs = 1000,
                HoldBottomMs = 500,
                Waveform = Waveform.Sine,
            });

            IReadOnlyList<RecordedWrite> writes = t.Snapshot();
            Assert.Equal(6, writes.Count);
            Assert.Equal(new byte[] { 0xB1, 5 }, writes[0].Bytes);
            Assert.Equal(new byte[] { 0xB2, 40 }, writes[1].Bytes);
            Assert.Equal(new byte[] { 0xB3, 10 }, writes[2].Bytes);
            Assert.Equal(new byte[] { 0xB4, 5 }, writes[3].Bytes);
            Assert.Equal(new byte[] { 0xB5, 0 }, writes[4].Bytes);
            Assert.Equal(new byte[] { 0xB0, 0x00 }, writes[5].Bytes);
        }

        [Fact]
        public async Task StartBreathe_WithStrobe_SetsTheModeByteToOne()
        {
            (MockTransport t, Glasses g) = Create();
            await g.StartBreatheAsync(new BreatheOptions { Bpm = 8, WithStrobe = true });

            IReadOnlyList<RecordedWrite> writes = t.Snapshot();
            Assert.Equal(new byte[] { 0xB0, 0x01 }, writes[1].Bytes);
        }

        [Fact]
        public async Task SyncBreath_DefaultsToFortyPercentInhale()
        {
            (MockTransport t, Glasses g) = Create();
            await g.SyncBreathAsync(5500);

            RecordedWrite write = Assert.Single(t.Snapshot());
            Assert.Equal(new byte[] { 0xBA, 0x7C, 0x15, 40 }, write.Bytes);
        }

        // -------------------------------------------------------------------
        // Preset sessions — order matches the Python SDK
        // -------------------------------------------------------------------

        [Fact]
        public async Task SessionRelax_SetsBrightnessThenBreatheThenDuration()
        {
            (MockTransport t, Glasses g) = Create();
            await g.SessionRelaxAsync(10);

            IReadOnlyList<RecordedWrite> writes = t.Snapshot();
            Assert.Equal(5, writes.Count);
            Assert.Equal(new byte[] { 0xA2, 100 }, writes[0].Bytes);
            Assert.Equal(new byte[] { 0xB1, 5 }, writes[1].Bytes);
            Assert.Equal(new byte[] { 0xB5, 0 }, writes[2].Bytes);
            Assert.Equal(new byte[] { 0xB0, 0x00 }, writes[3].Bytes);
            Assert.Equal(new byte[] { 0xA4, 10 }, writes[4].Bytes);
        }

        [Fact]
        public async Task SessionMeditate_UsesSixBpmSine()
        {
            (MockTransport t, Glasses g) = Create();
            await g.SessionMeditateAsync(10);

            IReadOnlyList<RecordedWrite> writes = t.Snapshot();
            Assert.Equal(4, writes.Count);
            Assert.Equal(new byte[] { 0xB1, 6 }, writes[0].Bytes);
            Assert.Equal(new byte[] { 0xB5, 0 }, writes[1].Bytes);
            Assert.Equal(new byte[] { 0xB0, 0x00 }, writes[2].Bytes);
            Assert.Equal(new byte[] { 0xA4, 10 }, writes[3].Bytes);
        }

        [Fact]
        public async Task SessionFocus_UsesTwelveHertzStrobeWithEightBpmBreathe()
        {
            (MockTransport t, Glasses g) = Create();
            await g.SessionFocusAsync(15);

            IReadOnlyList<RecordedWrite> writes = t.Snapshot();
            Assert.Equal(4, writes.Count);
            Assert.Equal(new byte[] { 0xAB, 12 }, writes[0].Bytes);
            Assert.Equal(new byte[] { 0xB1, 8 }, writes[1].Bytes);
            Assert.Equal(new byte[] { 0xB0, 0x01 }, writes[2].Bytes);
            Assert.Equal(new byte[] { 0xA4, 15 }, writes[3].Bytes);
        }

        [Fact]
        public async Task SessionSleep_UsesFourBpmSine()
        {
            (MockTransport t, Glasses g) = Create();
            await g.SessionSleepAsync(15);

            IReadOnlyList<RecordedWrite> writes = t.Snapshot();
            Assert.Equal(4, writes.Count);
            Assert.Equal(new byte[] { 0xB1, 4 }, writes[0].Bytes);
            Assert.Equal(new byte[] { 0xB5, 0 }, writes[1].Bytes);
            Assert.Equal(new byte[] { 0xB0, 0x00 }, writes[2].Bytes);
            Assert.Equal(new byte[] { 0xA4, 15 }, writes[3].Bytes);
        }

        // -------------------------------------------------------------------
        // Connection state and errors
        // -------------------------------------------------------------------

        [Fact]
        public async Task CommandsOnADisconnectedLinkThrow()
        {
            (MockTransport t, Glasses g) = Create();
            t.IsConnected = false;

            await Assert.ThrowsAsync<GlassesConnectionException>(() => g.SetStaticAsync(50));
            await Assert.ThrowsAsync<GlassesConnectionException>(() => g.SetOpacityAsync(10));
            await Assert.ThrowsAsync<GlassesConnectionException>(() => g.StreamStaticAsync(10));
            await Assert.ThrowsAsync<GlassesConnectionException>(() => g.GetBatteryAsync());
            Assert.Equal(0, t.WriteCount);
        }

        [Fact]
        public async Task ConnectionErrorIsAGlassesException()
        {
            (MockTransport t, Glasses g) = Create();
            t.IsConnected = false;

            await Assert.ThrowsAsync<GlassesConnectionException>(() => g.SetStaticAsync(50));
            // The base type catches it too, so callers can handle one type.
            GlassesException caught =
                await Assert.ThrowsAnyAsync<GlassesException>(() => g.SetStaticAsync(50));
            Assert.NotNull(caught.Message);
        }

        [Fact]
        public async Task TransportWriteFailuresSurfaceAsCommandException()
        {
            (MockTransport t, Glasses g) = Create();
            t.FailNextWrites(1);

            await Assert.ThrowsAsync<CommandException>(() => g.SetStaticAsync(50));
        }

        [Fact]
        public async Task ConnectAndDisconnectForwardToTheTransport()
        {
            (MockTransport t, Glasses g) = Create();

            await g.ConnectAsync(TimeSpan.FromSeconds(5));
            Assert.Equal(1, t.ConnectCalls);
            Assert.True(g.IsConnected);

            await g.DisconnectAsync();
            Assert.Equal(1, t.DisconnectCalls);
            Assert.False(g.IsConnected);
        }

        [Fact]
        public void NullTransportIsRejected()
        {
            Assert.Throws<ArgumentNullException>(() => new Glasses(null));
        }

        [Fact]
        public async Task DisposedGlassesThrowsObjectDisposed()
        {
            (MockTransport _, Glasses g) = Create();
            g.Dispose();

            await Assert.ThrowsAsync<ObjectDisposedException>(() => g.SetStaticAsync(50));
        }

        [Fact]
        public void DisposeReleasesTheTransportByDefault()
        {
            var transport = new MockTransport();
            using (new Glasses(transport))
            {
            }

            Assert.True(transport.Disposed);
        }

        [Fact]
        public void DisposeLeavesABorrowedTransportAlone()
        {
            var transport = new MockTransport();
            using (new Glasses(transport, ownsTransport: false))
            {
            }

            Assert.False(transport.Disposed);
        }

        // -------------------------------------------------------------------
        // Battery
        // -------------------------------------------------------------------

        [Fact]
        public async Task GetBattery_ReportsAbsenceAsNull()
        {
            (MockTransport t, Glasses g) = Create();
            t.BatteryLevel = null;

            Assert.Null(await g.GetBatteryAsync());
        }

        [Fact]
        public async Task GetBattery_ClampsTheReportedLevel()
        {
            (MockTransport t, Glasses g) = Create();

            t.BatteryLevel = 77;
            Assert.Equal(77, await g.GetBatteryAsync());

            t.BatteryLevel = 200;
            Assert.Equal(100, await g.GetBatteryAsync());

            t.BatteryLevel = -5;
            Assert.Equal(0, await g.GetBatteryAsync());
        }

        // -------------------------------------------------------------------
        // Fast write (firmware >= 4.16.3)
        // -------------------------------------------------------------------

        [Fact]
        public async Task StreamStatic_UsesWithResponseWhenFastWriteIsUnavailable()
        {
            (MockTransport t, Glasses g) = Create();
            t.SupportsWriteWithoutResponse = false;

            Assert.False(g.SupportsFastWrite);
            await g.StreamStaticAsync(42);

            RecordedWrite write = Assert.Single(t.Snapshot());
            Assert.Equal(new byte[] { 0xA5, 42 }, write.Bytes);
            Assert.True(write.WithResponse);
        }

        [Fact]
        public async Task StreamStatic_UsesWithoutResponseOnFirmware4163Plus()
        {
            (MockTransport t, Glasses g) = Create();
            t.SupportsWriteWithoutResponse = true;

            Assert.True(g.SupportsFastWrite);
            await g.StreamStaticAsync(42);

            RecordedWrite write = Assert.Single(t.Snapshot());
            Assert.False(write.WithResponse);
        }

        [Fact]
        public async Task CommandWritesStayAckedEvenOnFastWriteFirmware()
        {
            (MockTransport t, Glasses g) = Create();
            t.SupportsWriteWithoutResponse = true;

            await g.SetStaticAsync(42);
            await g.SetDurationAsync(60);

            Assert.All(t.Snapshot(), w => Assert.True(w.WithResponse));
        }

        [Fact]
        public void SupportsFastWriteIsFalseWhileDisconnected()
        {
            (MockTransport t, Glasses g) = Create();
            t.SupportsWriteWithoutResponse = true;
            t.IsConnected = false;

            Assert.False(g.SupportsFastWrite);
        }

        [Fact]
        public async Task StreamStaticClampsDuty()
        {
            (MockTransport t, Glasses g) = Create();

            await g.StreamStaticAsync(-10);
            await g.StreamStaticAsync(500);

            IReadOnlyList<RecordedWrite> writes = t.Snapshot();
            Assert.Equal(new byte[] { 0xA5, 0 }, writes[0].Bytes);
            Assert.Equal(new byte[] { 0xA5, 100 }, writes[1].Bytes);
        }

        // -------------------------------------------------------------------
        // Write serialization
        // -------------------------------------------------------------------

        [Fact]
        public async Task ConcurrentCallsNeverOverlapOnTheWire()
        {
            var transport = new OverlapDetectingTransport();
            using var glasses = new Glasses(transport);

            var tasks = new List<Task>();
            for (int i = 0; i < 50; i++)
            {
                int duty = i % 101;
                tasks.Add(Task.Run(() => glasses.SetStaticAsync(duty)));
                tasks.Add(Task.Run(() => glasses.StreamStaticAsync(duty)));
            }

            await Task.WhenAll(tasks);

            Assert.Equal(1, transport.MaxConcurrentWrites);
            Assert.Equal(100, transport.TotalWrites);
        }

        /// <summary>Fails the test if two writes are ever in flight at once.</summary>
        private sealed class OverlapDetectingTransport : IGlassesTransport
        {
            private int _inFlight;

            public int MaxConcurrentWrites { get; private set; } = 1;

            public int TotalWrites;

            public bool IsConnected => true;

            public bool SupportsWriteWithoutResponse => false;

            public Task ConnectAsync(TimeSpan timeout, CancellationToken cancellationToken) =>
                Task.CompletedTask;

            public Task DisconnectAsync() => Task.CompletedTask;

            public async Task WriteAsync(byte[] data, bool withResponse,
                CancellationToken cancellationToken)
            {
                int now = Interlocked.Increment(ref _inFlight);
                lock (this)
                {
                    if (now > MaxConcurrentWrites)
                    {
                        MaxConcurrentWrites = now;
                    }
                }

                await Task.Delay(1, cancellationToken).ConfigureAwait(false);
                Interlocked.Increment(ref TotalWrites);
                Interlocked.Decrement(ref _inFlight);
            }

            public Task<int?> ReadBatteryLevelAsync(CancellationToken cancellationToken) =>
                Task.FromResult<int?>(null);

            public void Dispose()
            {
            }
        }
    }
}
