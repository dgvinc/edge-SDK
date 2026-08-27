// SPDX-License-Identifier: MIT
//
// Using the SDK with a Bluetooth stack you already own.
//
// NeuroGuide-style applications often already have BLE code. You do not have to
// replace it: hand the SDK two callbacks and keep the protocol encoding,
// clamping, write serialization and the real-time feedback stream.
//
// This example runs with no hardware — the "GATT layer" just prints the frames,
// so you can see exactly what would go on the wire.
//
//     dotnet run --project csharp-SDK/examples/BringYourOwnBle

using Narbis.EdgeGlasses;

Console.WriteLine("EDGE Glasses — bring-your-own-BLE demo");
Console.WriteLine("======================================\n");

// --- Tier 1: no controller at all, just the frame bytes --------------------
// If all you want is the wire format, Protocol is pure and stateless.
Console.WriteLine("Protocol frames (write these to characteristic 0xFF01):");
PrintFrame("session guard, 60 min", Protocol.SetDuration(60));
PrintFrame("static 50 %", Protocol.SetStatic(50));
PrintFrame("legacy opacity 128", Protocol.Opacity(128));
PrintFrame("smoothing 80 ms", Protocol.SetLensSmoothing(80));
PrintFrame("sync 5500 ms / 40 %", Protocol.SyncBreath(5500, 40));
PrintFrame("static 999 -> clamped", Protocol.SetStatic(999));
// Anything the typed builders miss goes through Command(), which still enforces
// the >= 2-byte rule. Here: the deci-Hz strobe form for a 13.5 Hz target.
PrintFrame("strobe 13.5 Hz (deci-Hz)", Protocol.Command(0xAB, 135 & 0xFF, (135 >> 8) & 0xFF));

// --- Tier 2: the full controller over your stack ---------------------------
Console.WriteLine("\nController over a caller-supplied BLE stack:");

bool linkIsUp = true;

var transport = new CallbackTransport(
    writeAsync: (frame, withResponse, cancellationToken) =>
    {
        // Replace this body with your real GATT write. Throw to signal failure;
        // the SDK surfaces it as CommandException and the stream retries.
        Console.WriteLine($"    -> 0xFF01 ({(withResponse ? "with response" : "no response")})" +
                          $" {Convert.ToHexString(frame)}");
        return Task.CompletedTask;
    },
    isConnected: () => linkIsUp);

// Firmware >= 4.16.3 advertises write-without-response on 0xFF01. The built-in
// Windows transport detects this; with your own stack, declare it after
// checking the characteristic's properties.
transport.SupportsWriteWithoutResponse = false;

// Optional: let GetBatteryAsync work by pointing it at your 0x2A19 read.
transport.SetBatteryReader(_ => Task.FromResult<int?>(87));

using var glasses = new Glasses(transport);

Console.WriteLine("  session guard:");
await glasses.SetDurationAsync(60);

Console.WriteLine("  smoothing + slew cap:");
await glasses.SetLensSmoothingAsync(80);
await glasses.SetLensMaxRateAsync(40);

Console.WriteLine("  half tint:");
await glasses.SetStaticAsync(50);

Console.WriteLine($"  battery: {await glasses.GetBatteryAsync()} %");

Console.WriteLine("  feedback stream:");
FeedbackStream stream = glasses.StartFeedbackStream(30.0);

// In your app these come from your pipeline's callback, at any rate.
// FeedReward parks the value for the next writer tick.
stream.FeedReward(0.25);            // 25 % in condition -> 75 % tint
await Task.Delay(100);

// A discrete reward does NOT wait for a tick — it writes immediately.
await stream.RewardEventAsync(duty: 0, holdMs: 200);
await stream.StopAsync();           // stop and clear the lens

Console.WriteLine("\n  a dropped link surfaces as GlassesConnectionException:");
linkIsUp = false;
try
{
    await glasses.SetStaticAsync(10);
}
catch (GlassesConnectionException ex)
{
    Console.WriteLine("    " + ex.Message);
}

Console.WriteLine("\nOn Windows you can skip all of this and use WinRtBleTransport.");

static void PrintFrame(string label, byte[] frame)
{
    Console.WriteLine($"  {label,-28} {Convert.ToHexString(frame)}");
}
