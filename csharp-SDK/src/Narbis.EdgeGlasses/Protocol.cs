// SPDX-License-Identifier: MIT

using System;

namespace Narbis.EdgeGlasses
{
    /// <summary>Breathe waveform shape (opcode 0xB5).</summary>
    public enum Waveform
    {
        /// <summary>Cosine-shaped tint curve (device default).</summary>
        Sine = 0,

        /// <summary>Linear (triangle) tint curve.</summary>
        Linear = 1
    }

    /// <summary>
    /// Builds the exact byte frames the Narbis Edge firmware expects.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Pure and stateless, with no Bluetooth dependency: if your application
    /// already owns its BLE stack, use these directly and skip
    /// <see cref="Glasses"/> entirely.
    /// </para>
    /// <code>
    /// byte[] frame = Protocol.SetStatic(50);
    /// await myCharacteristic.WriteValueAsync(frame);
    /// </code>
    /// <para>
    /// The firmware never NACKs a command — bad arguments are silently clamped
    /// or dropped on the device. Every builder here clamps client-side, so what
    /// you send is what runs.
    /// </para>
    /// </remarks>
    public static class Protocol
    {
        /// <summary>Exact advertised device name. The service UUID is not advertised.</summary>
        public const string DeviceName = "Narbis_Edge";

        /// <summary>Custom service 0x00FF.</summary>
        public static readonly Guid ServiceUuid = ShortUuid(0x00FF);

        /// <summary>Control characteristic 0xFF01 — every command goes here.</summary>
        public static readonly Guid ControlUuid = ShortUuid(0xFF01);

        /// <summary>OTA data characteristic 0xFF02.</summary>
        public static readonly Guid OtaDataUuid = ShortUuid(0xFF02);

        /// <summary>Status characteristic 0xFF03 — the notification multiplexer.</summary>
        public static readonly Guid StatusUuid = ShortUuid(0xFF03);

        /// <summary>PPG stream characteristic 0xFF04.</summary>
        public static readonly Guid PpgUuid = ShortUuid(0xFF04);

        /// <summary>Standard Battery Service 0x180F (firmware 4.16.1+, V1.2+ hardware).</summary>
        public static readonly Guid BatteryServiceUuid = ShortUuid(0x180F);

        /// <summary>Standard Battery Level characteristic 0x2A19.</summary>
        public static readonly Guid BatteryLevelUuid = ShortUuid(0x2A19);

        /// <summary>Expand a 16-bit Bluetooth SIG short ID against the Bluetooth Base UUID.</summary>
        public static Guid ShortUuid(ushort id)
        {
            return new Guid(id, 0x0000, 0x1000, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB);
        }

        // -------------------------------------------------------------------
        // Opcodes — see docs/bluetooth-protocol.md §4.3
        // -------------------------------------------------------------------

        /// <summary>EMA glide between commanded static targets (fw 4.15.7+).</summary>
        public const byte OpLensSmoothing = 0xA0;

        /// <summary>Slew cap on commanded static transitions (fw 4.15.7+).</summary>
        public const byte OpLensMaxRate = 0xA1;

        /// <summary>Persistent max-tint / breathe depth.</summary>
        public const byte OpBrightness = 0xA2;

        /// <summary>Freeze (0) vs fail-clear (1) on link loss (fw 4.15.7+).</summary>
        public const byte OpDisconnectBehavior = 0xA3;

        /// <summary>Session length in minutes; the device auto-sleeps at expiry.</summary>
        public const byte OpDuration = 0xA4;

        /// <summary>Static duty — the real-time feedback opcode.</summary>
        public const byte OpStatic = 0xA5;

        /// <summary>Enter strobe mode.</summary>
        public const byte OpStartStrobe = 0xA6;

        /// <summary>Enter deep sleep now.</summary>
        public const byte OpSleep = 0xA7;

        /// <summary>Strobe frequency in Hz.</summary>
        public const byte OpStrobeFrequency = 0xAB;

        /// <summary>Strobe dark-phase duty.</summary>
        public const byte OpStrobeDuty = 0xAC;

        /// <summary>Enter breathe mode; arg 1 selects breathe+strobe (fw 4.15.6+).</summary>
        public const byte OpStartBreathe = 0xB0;

        /// <summary>Breathe rate in integer BPM.</summary>
        public const byte OpBreatheBpm = 0xB1;

        /// <summary>Inhale portion of the breath cycle.</summary>
        public const byte OpBreatheInhalePct = 0xB2;

        /// <summary>Hold at full-dark, in 100 ms units.</summary>
        public const byte OpBreatheHoldTop = 0xB3;

        /// <summary>Hold at clear, in 100 ms units.</summary>
        public const byte OpBreatheHoldBottom = 0xB4;

        /// <summary>Breathe waveform shape.</summary>
        public const byte OpBreatheWaveform = 0xB5;

        /// <summary>Phase-lock and exact cycle length (fw 4.15.5+).</summary>
        public const byte OpSyncBreath = 0xBA;

        /// <summary>Reset persisted settings to factory defaults.</summary>
        public const byte OpFactoryReset = 0xBF;

        /// <summary>Battery poll (fw 4.16.1+, V1.2+ hardware).</summary>
        public const byte OpBatteryPoll = 0xC7;

        // -------------------------------------------------------------------
        // Opacity — the legacy single-byte write
        // -------------------------------------------------------------------

        /// <summary>
        /// Legacy opacity write: ONE byte, 0 (clear) .. 255 (fully dark).
        /// </summary>
        /// <remarks>
        /// Intentionally a single byte — the firmware treats any 1-byte write as
        /// a direct lens duty and stops whatever mode is running. This is the
        /// only frame in the protocol allowed to be 1 byte long.
        /// </remarks>
        /// <param name="value">Opacity 0-255 (clamped).</param>
        public static byte[] Opacity(int value)
        {
            return new[] { (byte)Clamp(value, 0, 255) };
        }

        /// <summary>Fully clear lens (opacity 0).</summary>
        public static byte[] Clear()
        {
            return Opacity(0);
        }

        /// <summary>Fully dark lens (opacity 255).</summary>
        public static byte[] Dark()
        {
            return Opacity(255);
        }

        // -------------------------------------------------------------------
        // Parameters
        // -------------------------------------------------------------------

        /// <summary>
        /// Static mode at a fixed duty (0xA5) — the primary real-time dimming
        /// command. On firmware 4.16.2+ this does not touch the 0xA2 brightness.
        /// </summary>
        /// <param name="duty">
        /// 0-100 % (clamped). Duty 1..100 maps to a perceptual floor on the
        /// device (raw 265..1023); 0 is the only fully-clear value.
        /// </param>
        public static byte[] SetStatic(int duty)
        {
            return new[] { OpStatic, (byte)Clamp(duty, 0, 100) };
        }

        /// <summary>
        /// Persistent max-tint / breathe depth (0xA2). Persisted in NVS;
        /// multiplies the breathe / strobe / static output.
        /// </summary>
        /// <param name="percent">0-100 % (clamped).</param>
        public static byte[] SetBrightness(int percent)
        {
            return new[] { OpBrightness, (byte)Clamp(percent, 0, 100) };
        }

        /// <summary>
        /// Session duration (0xA4). Persisted; the device deep-sleeps at expiry.
        /// The clock runs from device wake, so this changes the total, not the origin.
        /// </summary>
        /// <param name="minutes">1-60 (clamped).</param>
        public static byte[] SetDuration(int minutes)
        {
            return new[] { OpDuration, (byte)Clamp(minutes, 1, 60) };
        }

        /// <summary>Strobe frequency (0xAB), integer-Hz form. Persisted.</summary>
        /// <param name="hz">1-50 Hz (clamped).</param>
        public static byte[] SetStrobeFrequency(int hz)
        {
            return new[] { OpStrobeFrequency, (byte)Clamp(hz, 1, 50) };
        }

        /// <summary>Strobe dark-phase duty (0xAC). Persisted.</summary>
        /// <param name="percent">10-90 % (clamped).</param>
        public static byte[] SetStrobeDuty(int percent)
        {
            return new[] { OpStrobeDuty, (byte)Clamp(percent, 10, 90) };
        }

        // -------------------------------------------------------------------
        // Lens config (firmware 4.15.7+; older firmware ignores these)
        // -------------------------------------------------------------------

        /// <summary>
        /// On-device lens smoothing (0xA0). Persisted in NVS.
        /// </summary>
        /// <remarks>
        /// The firmware glides between commanded static targets with an EMA of
        /// this time constant, so the lens moves continuously between your
        /// writes instead of stepping — the recommended way to get smooth
        /// real-time feedback. Send once at connect; tau ≈ 1-2× your write
        /// period (~80 ms is a good general value). Use firmware 4.15.9+ for a
        /// continuous stream.
        /// </remarks>
        /// <param name="milliseconds">0-2550 ms, 10 ms resolution. 0 = off (snap).</param>
        public static byte[] SetLensSmoothing(int milliseconds)
        {
            return new[] { OpLensSmoothing, (byte)Clamp(FloorDiv(milliseconds, 10), 0, 255) };
        }

        /// <summary>
        /// Hard slew cap on commanded static transitions (0xA1). Persisted.
        /// Applied after the 0xA0 glide — a safety envelope guaranteeing the
        /// lens cannot snap even if a host streams garbage. 40 ≈ full-scale in
        /// 250 ms.
        /// </summary>
        /// <param name="percentPer100Ms">0-100 %/100 ms. 0 = unlimited (default).</param>
        public static byte[] SetLensMaxRate(int percentPer100Ms)
        {
            return new[] { OpLensMaxRate, (byte)Clamp(percentPer100Ms, 0, 100) };
        }

        /// <summary>
        /// What the lens does when the BLE link drops (0xA3). Persisted.
        /// The factory default (false) FREEZES the lens at its last commanded
        /// output; true drops it to clear on link loss.
        /// </summary>
        public static byte[] SetDisconnectBehavior(bool failClear)
        {
            return new[] { OpDisconnectBehavior, failClear ? (byte)0x01 : (byte)0x00 };
        }

        // -------------------------------------------------------------------
        // Modes
        // -------------------------------------------------------------------

        /// <summary>Enter strobe mode (0xA6), padded to the 2-byte minimum.</summary>
        public static byte[] StartStrobe()
        {
            return new[] { OpStartStrobe, (byte)0x00 };
        }

        /// <summary>Breathe rate (0xB1), integer BPM. Use SyncBreath for fractional rates.</summary>
        /// <param name="bpm">1-30 (clamped).</param>
        public static byte[] SetBreatheBpm(int bpm)
        {
            return new[] { OpBreatheBpm, (byte)Clamp(bpm, 1, 30) };
        }

        /// <summary>Inhale portion of the breath cycle (0xB2).</summary>
        /// <param name="percent">10-90 % (clamped).</param>
        public static byte[] SetBreatheInhalePct(int percent)
        {
            return new[] { OpBreatheInhalePct, (byte)Clamp(percent, 10, 90) };
        }

        /// <summary>Hold at full-dark (0xB3), sent in 100 ms units.</summary>
        /// <param name="milliseconds">0-5000 ms (clamped, 100 ms resolution).</param>
        public static byte[] SetBreatheHoldTop(int milliseconds)
        {
            return new[] { OpBreatheHoldTop, (byte)Clamp(FloorDiv(milliseconds, 100), 0, 50) };
        }

        /// <summary>Hold at clear (0xB4), sent in 100 ms units.</summary>
        /// <param name="milliseconds">0-5000 ms (clamped, 100 ms resolution).</param>
        public static byte[] SetBreatheHoldBottom(int milliseconds)
        {
            return new[] { OpBreatheHoldBottom, (byte)Clamp(FloorDiv(milliseconds, 100), 0, 50) };
        }

        /// <summary>Breathe waveform shape (0xB5).</summary>
        public static byte[] SetBreatheWaveform(Waveform waveform)
        {
            return new[] { OpBreatheWaveform, waveform == Waveform.Linear ? (byte)1 : (byte)0 };
        }

        /// <summary>Enter breathe mode (0xB0).</summary>
        /// <param name="withStrobe">true = breathe+strobe (firmware 4.15.6+).</param>
        public static byte[] StartBreathe(bool withStrobe)
        {
            return new[] { OpStartBreathe, withStrobe ? (byte)0x01 : (byte)0x00 };
        }

        /// <summary>
        /// Phase-lock the breathe engine to an app-paced cycle (0xBA, fw 4.15.5+).
        /// </summary>
        /// <remarks>
        /// Wire format <c>[0xBA, cycleLo, cycleHi, inhalePct]</c> — cycle length
        /// as u16 little-endian. Restarts the breathe cosine at the instant of
        /// the write and sets the exact cycle length in ms, which is how you get
        /// fractional breathing rates. Send ONLY at the breath-cycle boundary;
        /// the override auto-expires 2 cycles after the last write.
        /// </remarks>
        /// <param name="cycleMs">Full breath cycle length in ms (clamped to u16).</param>
        /// <param name="inhalePct">Inhale portion 10-90 % (clamped).</param>
        public static byte[] SyncBreath(int cycleMs, int inhalePct)
        {
            int cycle = Clamp(cycleMs, 0, 65535);
            return new[]
            {
                OpSyncBreath,
                (byte)(cycle & 0xFF),
                (byte)((cycle >> 8) & 0xFF),
                (byte)Clamp(inhalePct, 10, 90)
            };
        }

        // -------------------------------------------------------------------
        // Power / maintenance
        // -------------------------------------------------------------------

        /// <summary>Enter deep sleep now (0xA7).</summary>
        public static byte[] Sleep()
        {
            return new[] { OpSleep, (byte)0x00 };
        }

        /// <summary>Reset all NVS-persisted settings to factory defaults (0xBF).</summary>
        public static byte[] FactoryReset()
        {
            return new[] { OpFactoryReset, (byte)0x00 };
        }

        /// <summary>Battery poll (0xC7, fw 4.16.1+ on V1.2+ hardware).</summary>
        /// <param name="reprobe">false = refresh, true = re-probe the ADC channel.</param>
        public static byte[] BatteryPoll(bool reprobe)
        {
            return new[] { OpBatteryPoll, reprobe ? (byte)0x01 : (byte)0x00 };
        }

        // -------------------------------------------------------------------
        // Escape hatch
        // -------------------------------------------------------------------

        /// <summary>
        /// Build an arbitrary opcode frame, enforcing the ≥ 2-byte rule.
        /// </summary>
        /// <remarks>
        /// A 1-byte write is the legacy opacity command, so an argument-less
        /// opcode is padded to <c>[opcode, 0x00]</c>. Use this for opcodes the
        /// typed builders do not cover — for example the 3-byte deci-Hz strobe
        /// form <c>[0xAB, dHzLo, dHzHi]</c> for sub-Hz entrainment targets.
        /// </remarks>
        /// <param name="opcode">Command opcode (low byte used).</param>
        /// <param name="payload">Optional argument bytes; may be null.</param>
        public static byte[] Command(int opcode, params byte[] payload)
        {
            int payloadLength = payload == null ? 0 : payload.Length;
            byte[] frame = new byte[Math.Max(2, 1 + payloadLength)];
            frame[0] = (byte)(opcode & 0xFF);
            if (payloadLength > 0)
            {
                Array.Copy(payload, 0, frame, 1, payloadLength);
            }

            // A single-byte write would be read as opacity; the extra slot is
            // already zero-initialised, which is the required 0x00 pad.
            return frame;
        }

        // -------------------------------------------------------------------

        internal static int Clamp(int value, int min, int max)
        {
            return value < min ? min : (value > max ? max : value);
        }

        internal static double Clamp(double value, double min, double max)
        {
            return value < min ? min : (value > max ? max : value);
        }

        /// <summary>
        /// Divide toward negative infinity, matching Python's <c>//</c> so the
        /// C#, C++ and Python SDKs agree on negative inputs.
        /// </summary>
        internal static int FloorDiv(int value, int divisor)
        {
            return (value < 0 && value % divisor != 0) ? (value / divisor - 1) : (value / divisor);
        }
    }
}
