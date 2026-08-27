// SPDX-License-Identifier: MIT
//
// Connect and drive the lens — the copy-paste minimum.
//
// Wake the glasses with a magnet tap first, then run this. It scans, connects,
// sets a session guard so the auto-sleep timer will not end things early, and
// steps the lens through clear -> half -> dark -> clear.
//
//     dotnet run --project csharp-SDK/examples/QuickStart

using Narbis.EdgeGlasses;

Console.WriteLine("Scanning for " + Protocol.DeviceName + "...");

IReadOnlyList<ScanResult> found = await WinRtBleTransport.ScanAsync(TimeSpan.FromSeconds(8));
if (found.Count == 0)
{
    Console.WriteLine(
        "No glasses found.\n" +
        "The radio powers down after 2 minutes with no client connected -\n" +
        "tap the magnet to the temple to wake them, then run this again.");
    return 1;
}

foreach (ScanResult device in found)
{
    Console.WriteLine("  " + device);
}

using var transport = new WinRtBleTransport(found[0].Address);
using var glasses = new Glasses(transport);

try
{
    await glasses.ConnectAsync(TimeSpan.FromSeconds(15));
    Console.WriteLine("Connected to " + transport.Address);

    // The glasses deep-sleep when the session timer expires, and the timer runs
    // from device wake - not from this write. Set it >= your session length.
    await glasses.SetDurationAsync(60);

    Console.WriteLine(glasses.SupportsFastWrite
        ? "Firmware >= 4.16.3: write-without-response available."
        : "Write-with-response only (firmware < 4.16.3).");

    int? battery = await glasses.GetBatteryAsync();
    Console.WriteLine(battery.HasValue
        ? $"Battery: {battery.Value} %"
        : "Battery: not reported by this unit.");

    Console.WriteLine("clear");
    await glasses.SetStaticAsync(0);
    await Task.Delay(1500);

    Console.WriteLine("half tint");
    await glasses.SetStaticAsync(50);
    await Task.Delay(1500);

    Console.WriteLine("fully dark");
    await glasses.SetStaticAsync(100);
    await Task.Delay(1500);

    // Always leave the wearer able to see: without this the lens FREEZES at its
    // last tint across the disconnect.
    Console.WriteLine("clear, disconnecting");
    await glasses.ClearAsync();
    await glasses.DisconnectAsync();
    return 0;
}
catch (DeviceNotFoundException)
{
    Console.Error.WriteLine("Glasses disappeared between the scan and the connect - "
        + "tap the magnet and retry.");
    return 1;
}
catch (GlassesException ex)
{
    Console.Error.WriteLine("Error: " + ex.Message);
    return 1;
}
