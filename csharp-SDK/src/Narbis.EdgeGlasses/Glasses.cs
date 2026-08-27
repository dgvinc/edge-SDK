// SPDX-License-Identifier: MIT

using System;
using System.Threading;
using System.Threading.Tasks;

namespace Narbis.EdgeGlasses
{
    /// <summary>
    /// Optional breathe parameters. Only the properties you set are written;
    /// everything else keeps its current (NVS-persisted) value on the device.
    /// </summary>
    public sealed class BreatheOptions
    {
        /// <summary>Breathing rate, 1-30 BPM. Integer only — use SyncBreathAsync for fractional rates.</summary>
        public int? Bpm { get; set; }

        /// <summary>Inhale portion of the cycle, 10-90 %.</summary>
        public int? InhalePct { get; set; }

        /// <summary>Hold at full-dark, 0-5000 ms (100 ms resolution).</summary>
        public int? HoldTopMs { get; set; }

        /// <summary>Hold at clear, 0-5000 ms (100 ms resolution).</summary>
        public int? HoldBottomMs { get; set; }

        /// <summary>Tint curve shape.</summary>
        public Waveform? Waveform { get; set; }

        /// <summary>
        /// Start breathe+strobe instead of plain breathe: the strobe's dark-phase
        /// duty is modulated by the breathing waveform (firmware 4.15.6+).
        /// </summary>
        public bool WithStrobe { get; set; }
    }

    /// <summary>
    /// EDGE Smart Glasses controller.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Mirrors the Python (<c>edge_glasses.Glasses</c>) and JavaScript SDKs
    /// method for method, so a protocol written against one ports to another by
    /// renaming.
    /// </para>
    /// <code>
    /// using var glasses = new Glasses(new WinRtBleTransport());
    /// await glasses.ConnectAsync();
    /// await glasses.SetDurationAsync(60);   // session guard
    /// await glasses.SetStaticAsync(50);     // 50 % tint
    /// </code>
    /// <para>
    /// All biofeedback processing runs app-side: the glasses are a display.
    /// Configure and start the firmware's breathe / static / strobe renderer, or
    /// stream static-duty writes for continuous feedback.
    /// </para>
    /// <para>
    /// The firmware never NACKs a command — bad arguments are silently clamped
    /// or dropped on the device. This SDK clamps every argument client-side, so
    /// what you send is what runs.
    /// </para>
    /// <para>
    /// Thread-safe: every write is serialized on an internal semaphore,
    /// satisfying the protocol's "never overlap writes to 0xFF01" rule even when
    /// a <see cref="FeedbackStream"/> writer and your own calls are both active.
    /// </para>
    /// </remarks>
    public sealed class Glasses : IDisposable
    {
        private readonly IGlassesTransport _transport;
        private readonly bool _ownsTransport;

        // Serializes every write to 0xFF01 (protocol doc §4.3). Concurrent GATT
        // writes to one characteristic fail outright on WinRT.
        private readonly SemaphoreSlim _writeLock = new SemaphoreSlim(1, 1);
        private bool _disposed;

        /// <summary>
        /// Create a controller over a transport.
        /// </summary>
        /// <param name="transport">The BLE link.</param>
        /// <param name="ownsTransport">
        /// When true (the default) <see cref="Dispose"/> also disposes the
        /// transport. Pass false if the transport outlives this object.
        /// </param>
        public Glasses(IGlassesTransport transport, bool ownsTransport = true)
        {
            _transport = transport ?? throw new ArgumentNullException(nameof(transport));
            _ownsTransport = ownsTransport;
        }

        // -------------------------------------------------------------------
        // Connection
        // -------------------------------------------------------------------

        /// <summary>True while connected.</summary>
        public bool IsConnected
        {
            get { return !_disposed && _transport.IsConnected; }
        }

        /// <summary>
        /// True if the control characteristic advertises write-without-response.
        /// </summary>
        /// <remarks>
        /// Firmware 4.16.3+ exposes write-without-response on 0xFF01, letting the
        /// real-time streaming path skip the ATT round-trip per write for higher
        /// sustained throughput. Older firmware is write-with-response only, and
        /// this stays false.
        /// </remarks>
        public bool SupportsFastWrite
        {
            get { return IsConnected && _transport.SupportsWriteWithoutResponse; }
        }

        /// <summary>
        /// Open the link, for transports that own their connection.
        /// </summary>
        /// <remarks>
        /// The glasses stop advertising and fully power down the radio after
        /// 2 minutes with no client connected. On
        /// <see cref="DeviceNotFoundException"/>, ask the user to tap the magnet
        /// to the temple to wake the device, then retry.
        /// </remarks>
        /// <param name="timeout">Overall budget for scan and connect.</param>
        /// <param name="cancellationToken">Cancels the attempt.</param>
        public Task ConnectAsync(TimeSpan? timeout = null,
            CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            return _transport.ConnectAsync(timeout ?? TimeSpan.FromSeconds(10), cancellationToken);
        }

        /// <summary>
        /// Close the link.
        /// </summary>
        /// <remarks>
        /// The lens FREEZES at its last commanded tint across a disconnect unless
        /// you enabled <see cref="SetDisconnectBehaviorAsync"/> — send
        /// <see cref="ClearAsync"/> first if the wearer should not be left dark.
        /// </remarks>
        public Task DisconnectAsync()
        {
            return _disposed ? Task.FromResult(true) : _transport.DisconnectAsync();
        }

        // -------------------------------------------------------------------
        // Low-level
        // -------------------------------------------------------------------

        /// <summary>
        /// Write raw bytes with write-with-response, serialized against every
        /// other write. Performs no padding, so the 1-byte legacy opacity write
        /// reaches the device intact.
        /// </summary>
        private async Task WriteRawAsync(byte[] frame, CancellationToken cancellationToken)
        {
            ThrowIfDisposed();
            if (!_transport.IsConnected)
            {
                throw new GlassesConnectionException("Not connected. Call ConnectAsync() first.");
            }

            await _writeLock.WaitAsync(cancellationToken).ConfigureAwait(false);
            try
            {
                await _transport.WriteAsync(frame, true, cancellationToken).ConfigureAwait(false);
            }
            finally
            {
                _writeLock.Release();
            }
        }

        /// <summary>
        /// Send a low-level opcode command.
        /// </summary>
        /// <remarks>
        /// Pads the total write to ≥ 2 bytes (a 1-byte write is the legacy
        /// opacity command). The firmware never NACKs — invalid opcodes or
        /// arguments are silently dropped or clamped on the device.
        /// </remarks>
        /// <example>
        /// <code>
        /// await glasses.SendCommandAsync(0xA2, 80);   // brightness 80 %
        /// await glasses.SendCommandAsync(0xA7);       // sleep, padded to [0xA7, 0x00]
        /// </code>
        /// </example>
        public Task SendCommandAsync(int opcode, params byte[] payload)
        {
            return WriteRawAsync(Protocol.Command(opcode, payload), CancellationToken.None);
        }

        /// <summary>Write an already-built protocol frame with write-with-response.</summary>
        public Task SendFrameAsync(byte[] frame, CancellationToken cancellationToken = default)
        {
            if (frame == null) throw new ArgumentNullException(nameof(frame));
            return WriteRawAsync(frame, cancellationToken);
        }

        /// <summary>
        /// Fast static-duty write for the real-time streaming path (0xA5).
        /// </summary>
        /// <remarks>
        /// Uses write-without-response when the control characteristic advertises
        /// it (firmware 4.16.3+), which lifts sustained throughput past the
        /// ~20/sec that per-write acks allow; otherwise falls back to
        /// write-with-response. Used by <see cref="FeedbackStream"/>. Command
        /// writes keep write-with-response for ordering and back-pressure.
        /// </remarks>
        /// <param name="duty">0-100 % (clamped).</param>
        /// <param name="cancellationToken">Cancels the write.</param>
        public async Task StreamStaticAsync(int duty,
            CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            if (!_transport.IsConnected)
            {
                throw new GlassesConnectionException("Not connected. Call ConnectAsync() first.");
            }

            byte[] frame = Protocol.SetStatic(duty);
            bool withResponse = !_transport.SupportsWriteWithoutResponse;

            await _writeLock.WaitAsync(cancellationToken).ConfigureAwait(false);
            try
            {
                await _transport.WriteAsync(frame, withResponse, cancellationToken)
                    .ConfigureAwait(false);
            }
            finally
            {
                _writeLock.Release();
            }
        }

        // -------------------------------------------------------------------
        // Opacity (legacy single-byte write)
        // -------------------------------------------------------------------

        /// <summary>
        /// Set lens opacity via the legacy single-byte write.
        /// </summary>
        /// <remarks>
        /// <para>
        /// Intentionally sends a single byte — the firmware treats any 1-byte
        /// write as a direct opacity command (0-255 → 0-100 % static duty) and
        /// stops whatever mode is currently running.
        /// </para>
        /// <para>
        /// Stream this for continuous real-time feedback. There is no 20 Hz
        /// protocol ceiling; target a configurable 30-50 Hz band with coalescing
        /// on. The BLE link paces you — write-with-response keeps exactly one
        /// write in flight, so the effective rate self-limits to your data rate
        /// (~8-11 writes/sec on default connection parameters, ~20/sec with
        /// throughput-optimized params).
        /// </para>
        /// </remarks>
        /// <param name="value">0 = clear .. 255 = fully dark (clamped).</param>
        /// <param name="cancellationToken">Cancels the write.</param>
        public Task SetOpacityAsync(int value, CancellationToken cancellationToken = default)
        {
            return WriteRawAsync(Protocol.Opacity(value), cancellationToken);
        }

        /// <summary>Set the lenses fully clear (transparent).</summary>
        public Task ClearAsync(CancellationToken cancellationToken = default)
        {
            return SetOpacityAsync(0, cancellationToken);
        }

        /// <summary>Set the lenses fully dark (opaque).</summary>
        public Task DarkAsync(CancellationToken cancellationToken = default)
        {
            return SetOpacityAsync(255, cancellationToken);
        }

        // -------------------------------------------------------------------
        // Parameters
        // -------------------------------------------------------------------

        /// <summary>
        /// Enter static mode at a fixed duty (0xA5) — the primary real-time
        /// dimming command. Stops the current mode and holds the lens at the
        /// given tint.
        /// </summary>
        /// <remarks>
        /// On firmware 4.16.2+ this is a clean static-duty write that does NOT
        /// touch the 0xA2 brightness / breathe depth, so you can stream dimming
        /// to 0 without zeroing the depth of the other programs. Duty 1-100 maps
        /// to a perceptual floor on the device; 0 is the only fully-clear value.
        /// </remarks>
        /// <param name="duty">0-100 % (clamped).</param>
        /// <param name="cancellationToken">Cancels the write.</param>
        public Task SetStaticAsync(int duty, CancellationToken cancellationToken = default)
        {
            return WriteRawAsync(Protocol.SetStatic(duty), cancellationToken);
        }

        /// <summary>
        /// Set the persistent max-tint / breathe depth (0xA2). Persisted in NVS.
        /// Does not change mode; this is the master tint level that MULTIPLIES
        /// the breathe / strobe / static output.
        /// </summary>
        /// <remarks>
        /// On firmware 4.16.2+ this is decoupled from
        /// <see cref="SetStaticAsync"/>. On firmware 4.16.1 and older the two
        /// shared one variable, so a SetStatic(0) left brightness at 0 and later
        /// breathe/strobe rendered clear until 0xA2 was re-sent.
        /// </remarks>
        /// <param name="percent">0-100 % (clamped).</param>
        /// <param name="cancellationToken">Cancels the write.</param>
        public Task SetBrightnessAsync(int percent, CancellationToken cancellationToken = default)
        {
            return WriteRawAsync(Protocol.SetBrightness(percent), cancellationToken);
        }

        /// <summary>
        /// Set session duration (0xA4). Persisted in NVS; the device auto-sleeps
        /// when the session ends.
        /// </summary>
        /// <remarks>
        /// The session clock runs from device wake, so writing this changes the
        /// total but does not restart it. Set it at session start, ≥ your planned
        /// length.
        /// </remarks>
        /// <param name="minutes">1-60 (clamped).</param>
        /// <param name="cancellationToken">Cancels the write.</param>
        public Task SetDurationAsync(int minutes, CancellationToken cancellationToken = default)
        {
            return WriteRawAsync(Protocol.SetDuration(minutes), cancellationToken);
        }

        /// <summary>
        /// Set strobe frequency (0xAB). Persisted in NVS; takes effect
        /// immediately if strobing.
        /// </summary>
        /// <param name="hz">1-50 Hz (clamped).</param>
        /// <param name="cancellationToken">Cancels the write.</param>
        public Task SetStrobeFrequencyAsync(int hz, CancellationToken cancellationToken = default)
        {
            return WriteRawAsync(Protocol.SetStrobeFrequency(hz), cancellationToken);
        }

        /// <summary>Set strobe dark-phase duty (0xAC). Persisted in NVS.</summary>
        /// <param name="percent">10-90 % (clamped).</param>
        /// <param name="cancellationToken">Cancels the write.</param>
        public Task SetStrobeDutyAsync(int percent, CancellationToken cancellationToken = default)
        {
            return WriteRawAsync(Protocol.SetStrobeDuty(percent), cancellationToken);
        }

        // -------------------------------------------------------------------
        // Lens config (firmware 4.15.7+; older firmware ignores these)
        // -------------------------------------------------------------------

        /// <summary>
        /// Set on-device lens smoothing (0xA0). Persisted in NVS.
        /// </summary>
        /// <remarks>
        /// <para>
        /// The firmware glides between commanded static targets with an EMA of
        /// this time constant, so the lens moves continuously between your writes
        /// instead of stepping — the recommended way to get smooth real-time
        /// feedback and to absorb per-sample noise without filtering
        /// client-side. Send it once at connect. Rule of thumb: tau ≈ 1-2× your
        /// write period (a 30 Hz stream is a ~33 ms period → ~35-70 ms; ~80 ms is
        /// a good general value).
        /// </para>
        /// <para>
        /// For a CONTINUOUS stream use firmware 4.15.9+: 4.15.7 stalls ~2-4 %
        /// short of each target (fixed in 4.15.8), and through 4.15.8 the
        /// smoothed output was still floored to ~101 duty levels. One-shot writes
        /// are fine on any firmware.
        /// </para>
        /// </remarks>
        /// <param name="milliseconds">0-2550 ms, 10 ms resolution. 0 = off (snap).</param>
        /// <param name="cancellationToken">Cancels the write.</param>
        public Task SetLensSmoothingAsync(int milliseconds,
            CancellationToken cancellationToken = default)
        {
            return WriteRawAsync(Protocol.SetLensSmoothing(milliseconds), cancellationToken);
        }

        /// <summary>
        /// Cap how fast the lens may transition (0xA1). Persisted in NVS.
        /// </summary>
        /// <remarks>
        /// A hard slew limit on commanded static transitions, applied after the
        /// smoothing glide — a safety envelope guaranteeing the lens cannot snap
        /// even if a host streams garbage. 40 corresponds to full-scale in
        /// ~250 ms. Does not affect breathe/strobe waveforms.
        /// </remarks>
        /// <param name="percentPer100Ms">0-100 %/100 ms. 0 = unlimited (factory default).</param>
        /// <param name="cancellationToken">Cancels the write.</param>
        public Task SetLensMaxRateAsync(int percentPer100Ms,
            CancellationToken cancellationToken = default)
        {
            return WriteRawAsync(Protocol.SetLensMaxRate(percentPer100Ms), cancellationToken);
        }

        /// <summary>
        /// Choose what the lens does when the BLE link drops (0xA3). Persisted.
        /// </summary>
        /// <remarks>
        /// The factory default (false) FREEZES the lens at its last commanded
        /// output across a disconnect — a crashed app leaves the last tint in
        /// place. With true the glasses instead stop any strobe and drop to a
        /// clear static lens on link loss. The failsafe fires when the firmware
        /// declares the link dead, bounded by the ~32 s supervision timeout, so
        /// still send an explicit <see cref="ClearAsync"/> before an intentional
        /// disconnect.
        /// </remarks>
        public Task SetDisconnectBehaviorAsync(bool failClear,
            CancellationToken cancellationToken = default)
        {
            return WriteRawAsync(Protocol.SetDisconnectBehavior(failClear), cancellationToken);
        }

        // -------------------------------------------------------------------
        // Modes
        // -------------------------------------------------------------------

        /// <summary>
        /// Start strobe mode (0xA6), optionally writing frequency and duty first.
        /// Omitted parameters keep their current (NVS-persisted) values.
        /// </summary>
        /// <param name="hz">Optional strobe frequency, 1-50 Hz.</param>
        /// <param name="dutyPct">Optional dark-phase duty, 10-90 %.</param>
        /// <param name="cancellationToken">Cancels the sequence.</param>
        public async Task StartStrobeAsync(int? hz = null, int? dutyPct = null,
            CancellationToken cancellationToken = default)
        {
            if (hz.HasValue)
            {
                await SetStrobeFrequencyAsync(hz.Value, cancellationToken).ConfigureAwait(false);
            }

            if (dutyPct.HasValue)
            {
                await SetStrobeDutyAsync(dutyPct.Value, cancellationToken).ConfigureAwait(false);
            }

            await WriteRawAsync(Protocol.StartStrobe(), cancellationToken).ConfigureAwait(false);
        }

        /// <summary>
        /// Start breathe mode (0xB0), writing only the parameters you pass.
        /// Omitted parameters keep their current (NVS-persisted) values.
        /// </summary>
        /// <remarks>
        /// With <see cref="BreatheOptions.WithStrobe"/> the strobe's dark-phase
        /// duty is modulated by the breathing waveform (firmware 4.15.6+).
        /// </remarks>
        public async Task StartBreatheAsync(BreatheOptions options = null,
            CancellationToken cancellationToken = default)
        {
            options = options ?? new BreatheOptions();

            if (options.Bpm.HasValue)
            {
                await WriteRawAsync(Protocol.SetBreatheBpm(options.Bpm.Value), cancellationToken)
                    .ConfigureAwait(false);
            }

            if (options.InhalePct.HasValue)
            {
                await WriteRawAsync(Protocol.SetBreatheInhalePct(options.InhalePct.Value),
                    cancellationToken).ConfigureAwait(false);
            }

            if (options.HoldTopMs.HasValue)
            {
                await WriteRawAsync(Protocol.SetBreatheHoldTop(options.HoldTopMs.Value),
                    cancellationToken).ConfigureAwait(false);
            }

            if (options.HoldBottomMs.HasValue)
            {
                await WriteRawAsync(Protocol.SetBreatheHoldBottom(options.HoldBottomMs.Value),
                    cancellationToken).ConfigureAwait(false);
            }

            if (options.Waveform.HasValue)
            {
                await WriteRawAsync(Protocol.SetBreatheWaveform(options.Waveform.Value),
                    cancellationToken).ConfigureAwait(false);
            }

            await WriteRawAsync(Protocol.StartBreathe(options.WithStrobe), cancellationToken)
                .ConfigureAwait(false);
        }

        /// <summary>
        /// Phase-lock the breathe engine to an app-paced cycle (0xBA).
        /// </summary>
        /// <remarks>
        /// <para>
        /// Restarts the breathe cosine at the instant of the write and sets the
        /// EXACT cycle length in milliseconds — this is how you get fractional
        /// breathing rates, since 0xB1 is integer-BPM only. Requires firmware
        /// 4.15.5+; older firmware ignores it, so it is always safe to send.
        /// </para>
        /// <para>
        /// IMPORTANT: send this only at the breath-cycle boundary (the start of
        /// an inhale), never mid-breath — the engine restarts its waveform
        /// immediately on receipt. The sync auto-expires 2 cycles after the last
        /// write, so re-send once per breath to stay locked.
        /// </para>
        /// </remarks>
        /// <param name="cycleMs">Full breath cycle length in ms (e.g. 5500 for 10.9 BPM).</param>
        /// <param name="inhalePct">Inhale portion of the cycle, 10-90 %.</param>
        /// <param name="cancellationToken">Cancels the write.</param>
        public Task SyncBreathAsync(int cycleMs, int inhalePct = 40,
            CancellationToken cancellationToken = default)
        {
            return WriteRawAsync(Protocol.SyncBreath(cycleMs, inhalePct), cancellationToken);
        }

        // -------------------------------------------------------------------
        // Power / maintenance
        // -------------------------------------------------------------------

        /// <summary>Put the glasses into deep sleep now (0xA7). Wake with a magnet tap.</summary>
        public Task SleepAsync(CancellationToken cancellationToken = default)
        {
            return WriteRawAsync(Protocol.Sleep(), cancellationToken);
        }

        /// <summary>Reset all NVS-persisted settings to factory defaults (0xBF).</summary>
        public Task FactoryResetAsync(CancellationToken cancellationToken = default)
        {
            return WriteRawAsync(Protocol.FactoryReset(), cancellationToken);
        }

        /// <summary>
        /// Read the battery charge level over the standard BLE Battery Service.
        /// </summary>
        /// <remarks>
        /// <para>
        /// Reads Battery Level (0x2A19) from the Battery Service (0x180F), which
        /// firmware 4.16.1+ exposes on V1.2+ hardware.
        /// </para>
        /// <para>
        /// On firmware 4.16.1+ the 0x2A19 characteristic is registered on every
        /// unit but reads 0 while the level is unknown — it cannot distinguish
        /// "unknown" from a genuine 0 %. To tell those apart (or to get
        /// millivolts and a charging flag) read the 0xFB status frame on 0xFF03
        /// instead: <c>[mv:u16 LE][soc:u8][charging:u8]</c>, where soc = 0xFF
        /// means unknown.
        /// </para>
        /// </remarks>
        /// <returns>
        /// Charge 0-100 %, or null if the device exposes no 0x180F Battery
        /// Service (pre-4.16.1 firmware, or a board without the battery divider).
        /// </returns>
        public async Task<int?> GetBatteryAsync(CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            if (!_transport.IsConnected)
            {
                throw new GlassesConnectionException("Not connected. Call ConnectAsync() first.");
            }

            int? level = await _transport.ReadBatteryLevelAsync(cancellationToken)
                .ConfigureAwait(false);
            return level.HasValue ? Protocol.Clamp(level.Value, 0, 100) : (int?)null;
        }

        // -------------------------------------------------------------------
        // Preset sessions
        // -------------------------------------------------------------------
        // Fixed-parameter presets: the firmware no longer ramps any parameter
        // over the session, so each preset just configures the breathe/strobe
        // engine and sets the auto-sleep duration.

        /// <summary>Relaxation: 5 BPM sine breathing at full brightness.</summary>
        public async Task SessionRelaxAsync(int durationMinutes = 10,
            CancellationToken cancellationToken = default)
        {
            await SetBrightnessAsync(100, cancellationToken).ConfigureAwait(false);
            await StartBreatheAsync(
                new BreatheOptions { Bpm = 5, Waveform = EdgeGlasses.Waveform.Sine },
                cancellationToken).ConfigureAwait(false);
            await SetDurationAsync(durationMinutes, cancellationToken).ConfigureAwait(false);
        }

        /// <summary>Meditation: 6 BPM sine breathing (the device default).</summary>
        public async Task SessionMeditateAsync(int durationMinutes = 10,
            CancellationToken cancellationToken = default)
        {
            await StartBreatheAsync(
                new BreatheOptions { Bpm = 6, Waveform = EdgeGlasses.Waveform.Sine },
                cancellationToken).ConfigureAwait(false);
            await SetDurationAsync(durationMinutes, cancellationToken).ConfigureAwait(false);
        }

        /// <summary>Focus: breathe+strobe, 12 Hz strobe modulated by 8 BPM breathing.</summary>
        public async Task SessionFocusAsync(int durationMinutes = 10,
            CancellationToken cancellationToken = default)
        {
            await SetStrobeFrequencyAsync(12, cancellationToken).ConfigureAwait(false);
            await StartBreatheAsync(new BreatheOptions { Bpm = 8, WithStrobe = true },
                cancellationToken).ConfigureAwait(false);
            await SetDurationAsync(durationMinutes, cancellationToken).ConfigureAwait(false);
        }

        /// <summary>Sleep preparation: 4 BPM sine breathing.</summary>
        public async Task SessionSleepAsync(int durationMinutes = 15,
            CancellationToken cancellationToken = default)
        {
            await StartBreatheAsync(
                new BreatheOptions { Bpm = 4, Waveform = EdgeGlasses.Waveform.Sine },
                cancellationToken).ConfigureAwait(false);
            await SetDurationAsync(durationMinutes, cancellationToken).ConfigureAwait(false);
        }

        // -------------------------------------------------------------------
        // Real-time feedback streaming
        // -------------------------------------------------------------------

        /// <summary>
        /// Open a plug-and-play real-time lens stream (the screen-dimmer pattern).
        /// </summary>
        /// <remarks>
        /// <para>
        /// Push a value from any thread at any rate via
        /// <see cref="FeedbackStream.Feed"/> / <see cref="FeedbackStream.FeedReward"/>;
        /// a background writer updates the lens at <paramref name="rateHz"/>,
        /// coalescing unchanged values and keeping exactly one write in flight.
        /// The rate is a target, never a queue.
        /// </para>
        /// <para>
        /// Proportional feedback (a dimmer that tracks your signal) uses Feed /
        /// FeedReward; discrete operant rewards use
        /// <see cref="FeedbackStream.RewardEventAsync"/>, which fires immediately
        /// instead of waiting for the next tick.
        /// </para>
        /// </remarks>
        /// <param name="rateHz">Writer rate, clamped to 1-45 Hz.</param>
        public FeedbackStream StartFeedbackStream(double rateHz = 30.0)
        {
            ThrowIfDisposed();
            return new FeedbackStream(this, rateHz);
        }

        // -------------------------------------------------------------------

        private void ThrowIfDisposed()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(Glasses));
            }
        }

        /// <summary>
        /// Release the write lock and, unless constructed with
        /// <c>ownsTransport: false</c>, the transport.
        /// </summary>
        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
            if (_ownsTransport)
            {
                _transport.Dispose();
            }

            _writeLock.Dispose();
        }
    }
}
