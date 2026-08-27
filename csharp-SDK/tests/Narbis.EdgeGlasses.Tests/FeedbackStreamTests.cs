// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Xunit;

namespace Narbis.EdgeGlasses.Tests
{
    public class FeedbackStreamTests
    {
        // The writer runs at 45 Hz (~22 ms) in most of these tests, so a 150 ms
        // wait covers several ticks even on a loaded CI machine.
        private const int SettleMs = 150;

        private static (MockTransport, Glasses) Create()
        {
            var transport = new MockTransport();
            return (transport, new Glasses(transport));
        }

        [Theory]
        [InlineData(1000.0, FeedbackStream.MaxRateHz)]
        [InlineData(0.01, FeedbackStream.MinRateHz)]
        [InlineData(30.0, 30.0)]
        public async Task RateIsClampedToTheSupportedBand(double requested, double expected)
        {
            (MockTransport _, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(requested);
            try
            {
                Assert.Equal(expected, stream.RateHz);
            }
            finally
            {
                await stream.StopAsync(false);
            }
        }

        [Fact]
        public async Task NothingIsWrittenBeforeTheFirstFeed()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            await Task.Delay(SettleMs);
            Assert.Equal(0, t.WriteCount);

            await stream.StopAsync(false);
        }

        [Fact]
        public async Task FeedReachesTheLensAsAStaticWrite()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            stream.Feed(60);
            await Task.Delay(SettleMs);

            Assert.Equal(new byte[] { 0xA5, 60 }, t.Snapshot()[0].Bytes);
            await stream.StopAsync(false);
        }

        [Fact]
        public async Task UnchangedValuesAreCoalescedAway()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            stream.Feed(30);
            await Task.Delay(250);   // ~11 ticks
            Assert.Equal(1, t.WriteCount);

            stream.Feed(30);         // same value: no write
            await Task.Delay(SettleMs);
            Assert.Equal(1, t.WriteCount);

            stream.Feed(31);         // changed: exactly one more write
            await Task.Delay(SettleMs);
            Assert.Equal(2, t.WriteCount);

            await stream.StopAsync(false);
        }

        [Theory]
        [InlineData(1.0, 0)]      // fully in condition -> clear
        [InlineData(0.0, 100)]    // fully out of condition -> dark
        [InlineData(0.25, 75)]
        [InlineData(0.5, 50)]
        public async Task FeedRewardMapsZeroToOneOntoDuty(double value, byte expectedDuty)
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            stream.FeedReward(value);
            await Task.Delay(SettleMs);

            Assert.Equal(new byte[] { 0xA5, expectedDuty }, t.Snapshot()[0].Bytes);
            await stream.StopAsync(false);
        }

        [Fact]
        public async Task FeedAndFeedRewardClampOutOfRangeInput()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            stream.Feed(500);
            await Task.Delay(SettleMs);
            Assert.Equal(new byte[] { 0xA5, 100 }, t.TakeWrites()[0].Bytes);

            stream.Feed(-500);
            await Task.Delay(SettleMs);
            Assert.Equal(new byte[] { 0xA5, 0 }, t.TakeWrites()[0].Bytes);

            stream.FeedReward(9.0);   // clamps to 1.0 -> duty 0, already current
            await Task.Delay(SettleMs);
            Assert.Empty(t.Snapshot());

            await stream.StopAsync(false);
        }

        [Fact]
        public async Task FeedRewardTreatsNaNAsOutOfCondition()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            stream.FeedReward(double.NaN);
            await Task.Delay(SettleMs);

            Assert.Equal(new byte[] { 0xA5, 100 }, t.Snapshot()[0].Bytes);
            await stream.StopAsync(false);
        }

        [Fact]
        public async Task RewardEventWritesImmediatelyWithoutWaitingForATick()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(1);   // a tick is a second away

            var started = DateTime.UtcNow;
            await stream.RewardEventAsync();
            var elapsed = DateTime.UtcNow - started;

            Assert.Equal(new byte[] { 0xA5, 0 }, t.Snapshot()[0].Bytes);
            Assert.True(elapsed < TimeSpan.FromMilliseconds(500),
                $"reward took {elapsed.TotalMilliseconds:F0} ms, well inside the 1000 ms tick");

            await stream.StopAsync(false);
        }

        [Fact]
        public async Task RewardEventHonoursANonZeroRewardTint()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(1);

            await stream.RewardEventAsync(20);

            Assert.Equal(new byte[] { 0xA5, 20 }, t.Snapshot()[0].Bytes);
            await stream.StopAsync(false);
        }

        [Fact]
        public async Task HoldMsSuppressesTheStreamThenItResumes()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            await stream.RewardEventAsync(0, holdMs: 300);
            t.TakeWrites();          // drop the reward write itself

            stream.Feed(90);         // the stream wants to darken immediately
            await Task.Delay(120);
            Assert.Equal(0, t.WriteCount);

            await Task.Delay(350);   // the hold expires
            Assert.Equal(new byte[] { 0xA5, 90 }, t.Snapshot().Last().Bytes);

            await stream.StopAsync(false);
        }

        [Fact]
        public async Task AFailedWriteIsRetriedOnTheNextTick()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            t.FailNextWrites(1);
            stream.Feed(55);
            await Task.Delay(250);

            Assert.Equal(1, t.FailedWrites);
            Assert.Equal(new byte[] { 0xA5, 55 }, t.Snapshot()[0].Bytes);

            await stream.StopAsync(false);
        }

        [Fact]
        public async Task StopClearsTheLensByDefaultAndIsIdempotent()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            stream.Feed(80);
            await Task.Delay(SettleMs);
            t.TakeWrites();

            await stream.StopAsync();
            RecordedWrite write = Assert.Single(t.Snapshot());
            Assert.Equal(new byte[] { 0x00 }, write.Bytes);   // 1-byte opacity 0
            Assert.False(stream.IsRunning);

            await stream.StopAsync();
            Assert.Single(t.Snapshot());   // the second stop writes nothing
        }

        [Fact]
        public async Task StopWithoutClearLeavesTheLastTintInPlace()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            stream.Feed(80);
            await Task.Delay(SettleMs);
            t.TakeWrites();

            await stream.StopAsync(false);
            Assert.Equal(0, t.WriteCount);
        }

        [Fact]
        public async Task StopOnADisconnectedLinkDoesNotThrow()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            stream.Feed(80);
            await Task.Delay(SettleMs);
            t.IsConnected = false;

            await stream.StopAsync();
            Assert.False(stream.IsRunning);
        }

        [Fact]
        public async Task StoppingHaltsTheWriter()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            stream.Feed(40);
            await Task.Delay(SettleMs);
            await stream.StopAsync(false);

            int after = t.WriteCount;
            stream.Feed(90);          // ignored: the writer is gone
            await Task.Delay(SettleMs);

            Assert.Equal(after, t.WriteCount);
        }

        [Fact]
        public async Task FeedFromManyThreadsProducesOnlyWellFormedFrames()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            using var cancellation = new CancellationTokenSource();
            var workers = new List<Task>();
            for (int i = 0; i < 4; i++)
            {
                int seed = i * 25;
                workers.Add(Task.Run(() =>
                {
                    int v = seed;
                    while (!cancellation.IsCancellationRequested)
                    {
                        stream.Feed(v);
                        v = (v + 1) % 101;
                    }
                }));
            }

            await Task.Delay(250);
            cancellation.Cancel();
            await Task.WhenAll(workers);
            await stream.StopAsync(false);

            Assert.All(t.Snapshot(), w =>
            {
                Assert.Equal(2, w.Bytes.Length);
                Assert.Equal(0xA5, w.Bytes[0]);
                Assert.InRange(w.Bytes[1], (byte)0, (byte)100);
            });
        }

        [Fact]
        public async Task RewardEventsAndTheStreamNeverProduceAPartialFrame()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            using var cancellation = new CancellationTokenSource();
            Task feeder = Task.Run(() =>
            {
                int v = 0;
                while (!cancellation.IsCancellationRequested)
                {
                    stream.Feed(v);
                    v = (v + 7) % 101;
                }
            });

            for (int i = 0; i < 30; i++)
            {
                await stream.RewardEventAsync();
                await Task.Delay(5);
            }

            cancellation.Cancel();
            await feeder;
            await stream.StopAsync(false);

            Assert.All(t.Snapshot(), w =>
            {
                Assert.Equal(2, w.Bytes.Length);
                Assert.Equal(0xA5, w.Bytes[0]);
                Assert.InRange(w.Bytes[1], (byte)0, (byte)100);
            });
        }

        [Fact]
        public async Task DisposeStopsTheWriterAndClears()
        {
            (MockTransport t, Glasses g) = Create();
            FeedbackStream stream = g.StartFeedbackStream(45);

            stream.Feed(40);
            await Task.Delay(SettleMs);
            t.TakeWrites();

            stream.Dispose();

            RecordedWrite write = Assert.Single(t.Snapshot());
            Assert.Equal(new byte[] { 0x00 }, write.Bytes);
            Assert.False(stream.IsRunning);
        }
    }
}
