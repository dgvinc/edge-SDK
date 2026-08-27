// SPDX-License-Identifier: MIT
//
// Hardware-free tests for WinRtBleTransport's pure helpers. The Bluetooth calls
// themselves need real glasses and are exercised by the manual smoke test in
// csharp-SDK/examples/QuickStart.

#if EDGE_WINRT

using System;
using System.Threading.Tasks;
using Xunit;

namespace Narbis.EdgeGlasses.Tests
{
    public class WinRtBleTransportTests
    {
        [Theory]
        [InlineData(0x001A2B3C4D5EUL, "00:1A:2B:3C:4D:5E")]
        [InlineData(0xAABBCCDDEEFFUL, "AA:BB:CC:DD:EE:FF")]
        [InlineData(0x000000000000UL, "00:00:00:00:00:00")]
        [InlineData(0xFFFFFFFFFFFFUL, "FF:FF:FF:FF:FF:FF")]
        public void FormatAddress_ProducesColonSeparatedUppercaseHex(ulong address, string expected)
        {
            Assert.Equal(expected, WinRtBleTransport.FormatAddress(address));
        }

        [Theory]
        [InlineData("AA:BB:CC:DD:EE:FF", 0xAABBCCDDEEFFUL)]
        [InlineData("aa:bb:cc:dd:ee:ff", 0xAABBCCDDEEFFUL)]
        [InlineData("AA-BB-CC-DD-EE-FF", 0xAABBCCDDEEFFUL)]
        [InlineData("AABBCCDDEEFF", 0xAABBCCDDEEFFUL)]
        [InlineData("00:1A:2B:3C:4D:5E", 0x001A2B3C4D5EUL)]
        public void TryParseAddress_AcceptsTheCommonFormats(string text, ulong expected)
        {
            Assert.True(WinRtBleTransport.TryParseAddress(text, out ulong actual));
            Assert.Equal(expected, actual);
        }

        [Theory]
        [InlineData(null)]
        [InlineData("")]
        [InlineData("   ")]
        [InlineData("AA:BB:CC")]              // too short
        [InlineData("AA:BB:CC:DD:EE:FF:00")]  // too long
        [InlineData("ZZ:BB:CC:DD:EE:FF")]     // not hex
        public void TryParseAddress_RejectsMalformedInput(string text)
        {
            Assert.False(WinRtBleTransport.TryParseAddress(text, out _));
        }

        [Fact]
        public void FormatAndParseRoundTrip()
        {
            const ulong original = 0x123456789ABCUL;
            Assert.True(WinRtBleTransport.TryParseAddress(
                WinRtBleTransport.FormatAddress(original), out ulong parsed));
            Assert.Equal(original, parsed);
        }

        [Fact]
        public void ConstructorRejectsAMalformedAddress()
        {
            Assert.Throws<ArgumentException>(() => new WinRtBleTransport("not-an-address"));
        }

        [Fact]
        public void ConstructorAcceptsAValidAddressAndExposesItNormalised()
        {
            using var transport = new WinRtBleTransport("aa:bb:cc:dd:ee:ff");
            Assert.Equal("AA:BB:CC:DD:EE:FF", transport.Address);
            Assert.False(transport.IsConnected);
            Assert.False(transport.SupportsWriteWithoutResponse);
        }

        [Fact]
        public async Task UsingADisposedTransportThrows()
        {
            var transport = new WinRtBleTransport();
            transport.Dispose();

            await Assert.ThrowsAsync<ObjectDisposedException>(() =>
                transport.WriteAsync(new byte[] { 0xA5, 50 }, true, default));
        }
    }
}

#endif
