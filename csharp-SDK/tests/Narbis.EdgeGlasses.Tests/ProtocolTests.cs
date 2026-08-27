// SPDX-License-Identifier: MIT
//
// Wire-format assertions. Every expected byte sequence is traceable to
// docs/bluetooth-protocol.md.

using Xunit;

namespace Narbis.EdgeGlasses.Tests
{
    public class ProtocolTests
    {
        // -------------------------------------------------------------------
        // The legacy single-byte opacity write
        // -------------------------------------------------------------------

        [Theory]
        [InlineData(0, 0x00)]
        [InlineData(128, 0x80)]
        [InlineData(255, 0xFF)]
        [InlineData(-1, 0x00)]     // clamped
        [InlineData(999, 0xFF)]    // clamped
        public void Opacity_IsAlwaysExactlyOneByte(int value, byte expected)
        {
            byte[] frame = Protocol.Opacity(value);
            Assert.Single(frame);
            Assert.Equal(expected, frame[0]);
        }

        [Fact]
        public void Opacity_IsNeverPaddedToTwoBytes()
        {
            // 0xA6 and 0xA7 as opacity values would look like opcodes; the
            // firmware reads any 1-byte write as opacity, so they must stay 1 byte.
            Assert.Single(Protocol.Opacity(0xA6));
            Assert.Single(Protocol.Opacity(0xA7));
        }

        [Fact]
        public void ClearAndDark_AreTheOpacityExtremes()
        {
            Assert.Equal(new byte[] { 0x00 }, Protocol.Clear());
            Assert.Equal(new byte[] { 0xFF }, Protocol.Dark());
        }

        // -------------------------------------------------------------------
        // Parameters
        // -------------------------------------------------------------------

        [Theory]
        [InlineData(0, 0)]
        [InlineData(50, 50)]
        [InlineData(100, 100)]
        [InlineData(-5, 0)]
        [InlineData(255, 100)]
        public void SetStatic_ClampsTo0To100(int duty, byte expected)
        {
            Assert.Equal(new byte[] { 0xA5, expected }, Protocol.SetStatic(duty));
        }

        [Theory]
        [InlineData(80, 80)]
        [InlineData(-1, 0)]
        [InlineData(101, 100)]
        public void SetBrightness_ClampsTo0To100(int percent, byte expected)
        {
            Assert.Equal(new byte[] { 0xA2, expected }, Protocol.SetBrightness(percent));
        }

        [Theory]
        [InlineData(60, 60)]
        [InlineData(0, 1)]      // clamped up to the 1-minute minimum
        [InlineData(120, 60)]   // clamped down to the 60-minute maximum
        public void SetDuration_ClampsTo1To60(int minutes, byte expected)
        {
            Assert.Equal(new byte[] { 0xA4, expected }, Protocol.SetDuration(minutes));
        }

        [Theory]
        [InlineData(10, 10)]
        [InlineData(0, 1)]
        [InlineData(99, 50)]
        public void SetStrobeFrequency_ClampsTo1To50(int hz, byte expected)
        {
            Assert.Equal(new byte[] { 0xAB, expected }, Protocol.SetStrobeFrequency(hz));
        }

        [Theory]
        [InlineData(50, 50)]
        [InlineData(0, 10)]
        [InlineData(100, 90)]
        public void SetStrobeDuty_ClampsTo10To90(int percent, byte expected)
        {
            Assert.Equal(new byte[] { 0xAC, expected }, Protocol.SetStrobeDuty(percent));
        }

        // -------------------------------------------------------------------
        // Lens config
        // -------------------------------------------------------------------

        [Theory]
        [InlineData(0, 0)]        // off (snap)
        [InlineData(80, 8)]       // the recommended general-purpose value
        [InlineData(100, 10)]
        [InlineData(85, 8)]       // truncates to 10 ms resolution
        [InlineData(2550, 255)]   // the maximum time constant
        [InlineData(9999, 255)]   // clamped
        [InlineData(-50, 0)]      // clamped
        public void SetLensSmoothing_ConvertsMillisecondsToTau(int ms, byte expected)
        {
            Assert.Equal(new byte[] { 0xA0, expected }, Protocol.SetLensSmoothing(ms));
        }

        [Theory]
        [InlineData(5, 5)]
        [InlineData(-1, 0)]
        [InlineData(200, 100)]
        public void SetLensMaxRate_ClampsTo0To100(int rate, byte expected)
        {
            Assert.Equal(new byte[] { 0xA1, expected }, Protocol.SetLensMaxRate(rate));
        }

        [Fact]
        public void SetDisconnectBehavior_EncodesFreezeVersusFailClear()
        {
            Assert.Equal(new byte[] { 0xA3, 0x00 }, Protocol.SetDisconnectBehavior(false));
            Assert.Equal(new byte[] { 0xA3, 0x01 }, Protocol.SetDisconnectBehavior(true));
        }

        // -------------------------------------------------------------------
        // Breathe
        // -------------------------------------------------------------------

        [Theory]
        [InlineData(6, 6)]
        [InlineData(0, 1)]
        [InlineData(99, 30)]
        public void SetBreatheBpm_ClampsTo1To30(int bpm, byte expected)
        {
            Assert.Equal(new byte[] { 0xB1, expected }, Protocol.SetBreatheBpm(bpm));
        }

        [Theory]
        [InlineData(40, 40)]
        [InlineData(5, 10)]
        [InlineData(95, 90)]
        public void SetBreatheInhalePct_ClampsTo10To90(int pct, byte expected)
        {
            Assert.Equal(new byte[] { 0xB2, expected }, Protocol.SetBreatheInhalePct(pct));
        }

        [Theory]
        [InlineData(1000, 10)]    // 100 ms units
        [InlineData(500, 5)]
        [InlineData(9999, 50)]    // clamped to the 5000 ms maximum
        [InlineData(-100, 0)]
        public void SetBreatheHoldTop_ConvertsTo100MsUnits(int ms, byte expected)
        {
            Assert.Equal(new byte[] { 0xB3, expected }, Protocol.SetBreatheHoldTop(ms));
        }

        [Theory]
        [InlineData(500, 5)]
        [InlineData(-100, 0)]
        public void SetBreatheHoldBottom_ConvertsTo100MsUnits(int ms, byte expected)
        {
            Assert.Equal(new byte[] { 0xB4, expected }, Protocol.SetBreatheHoldBottom(ms));
        }

        [Fact]
        public void SetBreatheWaveform_EncodesSineAndLinear()
        {
            Assert.Equal(new byte[] { 0xB5, 0 }, Protocol.SetBreatheWaveform(Waveform.Sine));
            Assert.Equal(new byte[] { 0xB5, 1 }, Protocol.SetBreatheWaveform(Waveform.Linear));
        }

        [Fact]
        public void StartBreathe_SelectsPlainOrStrobeVariant()
        {
            Assert.Equal(new byte[] { 0xB0, 0x00 }, Protocol.StartBreathe(false));
            Assert.Equal(new byte[] { 0xB0, 0x01 }, Protocol.StartBreathe(true));
        }

        // -------------------------------------------------------------------
        // SyncBreath — u16 little-endian on the wire
        // -------------------------------------------------------------------

        [Theory]
        [InlineData(5500, 40, 0x7C, 0x15, 40)]     // 5500 = 0x157C
        [InlineData(10000, 50, 0x10, 0x27, 50)]    // 10000 = 0x2710
        [InlineData(255, 40, 0xFF, 0x00, 40)]      // low byte only
        [InlineData(256, 40, 0x00, 0x01, 40)]      // high byte only
        [InlineData(70000, 40, 0xFF, 0xFF, 40)]    // clamped to u16
        [InlineData(-1, 40, 0x00, 0x00, 40)]       // clamped at zero
        [InlineData(5000, 5, 0x88, 0x13, 10)]      // inhale clamped up
        [InlineData(5000, 99, 0x88, 0x13, 90)]     // inhale clamped down
        public void SyncBreath_PacksCycleAsLittleEndianU16(
            int cycleMs, int inhalePct, byte lo, byte hi, byte inhale)
        {
            Assert.Equal(new byte[] { 0xBA, lo, hi, inhale },
                Protocol.SyncBreath(cycleMs, inhalePct));
        }

        // -------------------------------------------------------------------
        // The >= 2-byte rule
        // -------------------------------------------------------------------

        [Fact]
        public void ArgumentlessOpcodes_ArePaddedWithZero()
        {
            // A bare 1-byte write would be read as opacity, not as a command.
            Assert.Equal(new byte[] { 0xA6, 0x00 }, Protocol.StartStrobe());
            Assert.Equal(new byte[] { 0xA7, 0x00 }, Protocol.Sleep());
            Assert.Equal(new byte[] { 0xBF, 0x00 }, Protocol.FactoryReset());
            Assert.Equal(new byte[] { 0xC7, 0x00 }, Protocol.BatteryPoll(false));
            Assert.Equal(new byte[] { 0xC7, 0x01 }, Protocol.BatteryPoll(true));
        }

        [Fact]
        public void Command_PadsWhenThereIsNoPayload()
        {
            Assert.Equal(new byte[] { 0xA6, 0x00 }, Protocol.Command(0xA6));
            Assert.Equal(new byte[] { 0xB0, 0x00 }, Protocol.Command(0xB0));
        }

        [Fact]
        public void Command_DoesNotPadWhenAPayloadIsSupplied()
        {
            Assert.Equal(new byte[] { 0xA2, 80 }, Protocol.Command(0xA2, 80));
            Assert.Equal(new byte[] { 0xBA, 0x7C, 0x15, 40 },
                Protocol.Command(0xBA, 0x7C, 0x15, 40));
        }

        [Fact]
        public void Command_HandlesNullPayload()
        {
            Assert.Equal(new byte[] { 0xA7, 0x00 }, Protocol.Command(0xA7, null));
        }

        [Fact]
        public void Command_SupportsTheDeciHertzStrobeForm()
        {
            // Protocol doc §4.6.6: [0xAB, dHz_lo, dHz_hi] for sub-Hz precision.
            // 13.5 Hz -> 135 -> 0x0087.
            Assert.Equal(new byte[] { 0xAB, 0x87, 0x00 }, Protocol.Command(0xAB, 0x87, 0x00));
        }

        [Fact]
        public void EveryCommandFrameExceptOpacityIsAtLeastTwoBytes()
        {
            byte[][] frames =
            {
                Protocol.SetStatic(50), Protocol.SetBrightness(50), Protocol.SetDuration(30),
                Protocol.SetStrobeFrequency(10), Protocol.SetStrobeDuty(50),
                Protocol.SetLensSmoothing(80), Protocol.SetLensMaxRate(5),
                Protocol.SetDisconnectBehavior(true), Protocol.SetBreatheBpm(6),
                Protocol.SetBreatheInhalePct(40), Protocol.SetBreatheHoldTop(0),
                Protocol.SetBreatheHoldBottom(0), Protocol.SetBreatheWaveform(Waveform.Sine),
                Protocol.StartBreathe(false), Protocol.StartStrobe(), Protocol.Sleep(),
                Protocol.FactoryReset(), Protocol.SyncBreath(5000, 40),
                Protocol.BatteryPoll(false),
            };

            Assert.All(frames, f => Assert.True(f.Length >= 2,
                $"opcode 0x{f[0]:X2} produced a {f.Length}-byte frame"));
        }

        // -------------------------------------------------------------------
        // UUIDs
        // -------------------------------------------------------------------

        [Fact]
        public void ShortUuid_ExpandsAgainstTheBluetoothBaseUuid()
        {
            Assert.Equal("000000ff-0000-1000-8000-00805f9b34fb", Protocol.ServiceUuid.ToString());
            Assert.Equal("0000ff01-0000-1000-8000-00805f9b34fb", Protocol.ControlUuid.ToString());
            Assert.Equal("0000ff02-0000-1000-8000-00805f9b34fb", Protocol.OtaDataUuid.ToString());
            Assert.Equal("0000ff03-0000-1000-8000-00805f9b34fb", Protocol.StatusUuid.ToString());
            Assert.Equal("0000ff04-0000-1000-8000-00805f9b34fb", Protocol.PpgUuid.ToString());
            Assert.Equal("0000180f-0000-1000-8000-00805f9b34fb",
                Protocol.BatteryServiceUuid.ToString());
            Assert.Equal("00002a19-0000-1000-8000-00805f9b34fb",
                Protocol.BatteryLevelUuid.ToString());
        }

        [Fact]
        public void DeviceName_IsTheExactAdvertisedName()
        {
            Assert.Equal("Narbis_Edge", Protocol.DeviceName);
        }

        // -------------------------------------------------------------------
        // Internal helpers
        // -------------------------------------------------------------------

        [Theory]
        [InlineData(85, 10, 8)]
        [InlineData(80, 10, 8)]
        [InlineData(-5, 10, -1)]    // floors toward negative infinity, like Python's //
        [InlineData(-10, 10, -1)]
        [InlineData(0, 10, 0)]
        public void FloorDiv_MatchesPythonFloorDivision(int value, int divisor, int expected)
        {
            Assert.Equal(expected, Protocol.FloorDiv(value, divisor));
        }
    }
}
