// SPDX-License-Identifier: MIT

using System;
using System.Threading;
using System.Threading.Tasks;

namespace Narbis.EdgeGlasses
{
    /// <summary>
    /// Push-style real-time lens control — a wearable screen dimmer.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Created via <see cref="Glasses.StartFeedbackStream"/>. Call
    /// <see cref="Feed"/> / <see cref="FeedReward"/> from anywhere — a BLE
    /// notification handler, an LSL callback, a UDP reader, your DSP thread — at
    /// any rate; a background writer decimates to the stream rate, skips
    /// unchanged values, and never overlaps BLE writes. A failed write resets
    /// the coalesce key so the next tick retries.
    /// </para>
    /// <code>
    /// var stream = glasses.StartFeedbackStream();      // ~30 Hz writer
    /// myPipeline.OnUpdate += v => stream.FeedReward(v); // 0..1, any thread, any rate
    /// await stream.RewardEventAsync(0, 150);            // discrete reward, delivered now
    /// await stream.StopAsync();                         // stops and clears the lens
    /// </code>
    /// <para>The stream must not outlive the <see cref="Glasses"/> that created it.</para>
    /// </remarks>
    public sealed class FeedbackStream : IDisposable
    {
        /// <summary>Lowest writer rate, Hz.</summary>
        public const double MinRateHz = 1.0;

        /// <summary>
        /// Highest writer rate, Hz. Above this the BLE link, not the timer, is
        /// the limit — see the rate contract in the protocol doc §4.6.1.
        /// </summary>
        public const double MaxRateHz = 45.0;

        private readonly Glasses _glasses;
        private readonly TimeSpan _interval;
        private readonly CancellationTokenSource _cancellation = new CancellationTokenSource();
        private readonly Task _writerTask;

        // Gives RewardEventAsync priority over the writer tick: a reward waits on
        // this lock, while the writer tries it and skips a tick rather than
        // queueing behind a reward. Also guards _lastSent.
        private readonly SemaphoreSlim _streamLock = new SemaphoreSlim(1, 1);

        private int _duty = -1;              // latest requested duty; -1 before the first Feed
        private long _holdUntilTicks;        // Stopwatch-style deadline for a reward hold
        private int _lastSent = -1;
        private int _stopped;                // 0 = running, 1 = stopped (Interlocked)

        internal FeedbackStream(Glasses glasses, double rateHz)
        {
            _glasses = glasses;
            if (double.IsNaN(rateHz))
            {
                rateHz = 30.0;
            }

            RateHz = Protocol.Clamp(rateHz, MinRateHz, MaxRateHz);
            _interval = TimeSpan.FromSeconds(1.0 / RateHz);
            _writerTask = Task.Run(() => RunAsync(_cancellation.Token));
        }

        /// <summary>Configured writer rate in Hz, after clamping.</summary>
        public double RateHz { get; }

        /// <summary>True while the writer is running.</summary>
        public bool IsRunning
        {
            get { return Volatile.Read(ref _stopped) == 0; }
        }

        /// <summary>
        /// Request a lens duty: 0 = clear .. 100 = fully dark.
        /// </summary>
        /// <remarks>
        /// Cheap, lock-free and safe to call at any rate from any thread; only
        /// changed values reach BLE. Use this for PROPORTIONAL feedback (a dimmer
        /// that tracks your signal).
        /// </remarks>
        /// <param name="duty">0-100 (clamped).</param>
        public void Feed(int duty)
        {
            Interlocked.Exchange(ref _duty, Protocol.Clamp(duty, 0, 100));
        }

        /// <summary>
        /// Request tint from a 0..1 reward value (1 = in condition = clear).
        /// The classic dimmer mapping: duty = (1 - value) × 100.
        /// </summary>
        /// <param name="value">0.0-1.0 (clamped; NaN is treated as 0 = out of condition).</param>
        public void FeedReward(double value)
        {
            if (double.IsNaN(value))
            {
                value = 0.0;
            }

            value = Protocol.Clamp(value, 0.0, 1.0);
            Feed((int)Math.Round((1.0 - value) * 100.0, MidpointRounding.AwayFromZero));
        }

        /// <summary>
        /// Deliver a DISCRETE reward now, bypassing the stream tick.
        /// </summary>
        /// <remarks>
        /// For operant conditioning: call the instant your detector crosses
        /// threshold. Unlike <see cref="Feed"/>, which parks the value for the
        /// next scheduled tick (up to one stream period later), this writes
        /// immediately — latency is just the BLE transport (~20-60 ms), with no
        /// cadence jitter. It preempts the proportional stream, waiting at most
        /// one in-flight write. Write failures are swallowed, matching the
        /// stream's retry-next-tick behaviour.
        /// </remarks>
        /// <param name="duty">Reward tint 0-100 (default 0 = fully clear = positive reward).</param>
        /// <param name="holdMs">
        /// Hold the reward tint this long before the proportional stream resumes
        /// (0 = let the next Feed value take back over immediately).
        /// </param>
        public async Task RewardEventAsync(int duty = 0, int holdMs = 0)
        {
            duty = Protocol.Clamp(duty, 0, 100);

            // Blocking: waits out at most one in-flight tick write, never a queue.
            await _streamLock.WaitAsync().ConfigureAwait(false);
            try
            {
                await WriteLockedAsync(duty).ConfigureAwait(false);
            }
            finally
            {
                _streamLock.Release();
            }

            if (holdMs > 0)
            {
                Interlocked.Exchange(ref _holdUntilTicks,
                    DateTime.UtcNow.Ticks + (long)holdMs * TimeSpan.TicksPerMillisecond);
            }
        }

        /// <summary>
        /// Stop the writer.
        /// </summary>
        /// <param name="clearLens">
        /// By default clears the lens — it otherwise FREEZES at the last tint
        /// across a disconnect (protocol doc §2.5).
        /// </param>
        /// <remarks>Idempotent; a second call does nothing.</remarks>
        public async Task StopAsync(bool clearLens = true)
        {
            if (Interlocked.Exchange(ref _stopped, 1) != 0)
            {
                return;
            }

            _cancellation.Cancel();
            try
            {
                await _writerTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                // Expected: the writer observes cancellation through its delay.
            }

            if (clearLens && _glasses.IsConnected)
            {
                await _glasses.ClearAsync().ConfigureAwait(false);
            }
        }

        /// <summary>
        /// Write one duty value. The caller must hold <see cref="_streamLock"/>.
        /// </summary>
        private async Task WriteLockedAsync(int duty)
        {
            _lastSent = duty;   // claim before the write, so a concurrent tick coalesces
            try
            {
                await _glasses.StreamStaticAsync(duty).ConfigureAwait(false);
            }
            catch (Exception)
            {
                // A dropped write is normal on a congested link: reset the
                // coalesce key so the next tick retries this value.
                _lastSent = -1;
            }
        }

        private async Task RunAsync(CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                int duty = Volatile.Read(ref _duty);
                long holdUntil = Interlocked.Read(ref _holdUntilTicks);

                if (duty >= 0 && DateTime.UtcNow.Ticks >= holdUntil)
                {
                    // Wait(0), not Wait(): a RewardEventAsync in progress owns the
                    // wire, and this tick yields to it rather than queueing behind.
                    if (_streamLock.Wait(0))
                    {
                        try
                        {
                            if (duty != _lastSent)
                            {
                                await WriteLockedAsync(duty).ConfigureAwait(false);
                            }
                        }
                        finally
                        {
                            _streamLock.Release();
                        }
                    }
                }

                try
                {
                    await Task.Delay(_interval, cancellationToken).ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                {
                    return;
                }
            }
        }

        /// <summary>
        /// Stop the writer and clear the lens, blocking until done.
        /// </summary>
        /// <remarks>
        /// Leaving a wearer dark is the bad failure mode, so disposal clears by
        /// default. Errors are suppressed — call <see cref="StopAsync"/>
        /// explicitly if you need to observe them or keep the last tint.
        /// </remarks>
        public void Dispose()
        {
            try
            {
                StopAsync().GetAwaiter().GetResult();
            }
            catch (Exception)
            {
                // Dispose must not throw.
            }

            _cancellation.Dispose();
            _streamLock.Dispose();
        }
    }
}
