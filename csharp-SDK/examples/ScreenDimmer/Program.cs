// SPDX-License-Identifier: MIT
//
// The wearable screen dimmer — the primary third-party integration pattern.
//
// Classic neurofeedback dims the training display when the trainee falls out of
// condition and clears it when they are in condition. The Edge does the same on
// the lens itself, so it drops into any protocol (SMR, alpha/theta, HEG, EMG
// down-training, HRV...) wherever your software already produces a 0..1
// feedback value.
//
// Wire your pipeline's callback straight to stream.FeedReward(). The stream
// handles decimation, coalescing and write serialization; you never think about
// BLE cadence. For a DISCRETE operant reward, call RewardEventAsync() instead —
// it bypasses the tick so reinforcement latency is transport-only.
//
// This example substitutes a synthetic signal for a real EEG index.
//
//     dotnet run --project csharp-SDK/examples/ScreenDimmer

using Narbis.EdgeGlasses;

// Stand-in for your pipeline. Replace with your real feedback value, 0..1,
// where 1 = in condition.
static double SyntheticFeedback(double seconds)
{
    double slow = 0.5 + (0.5 * Math.Sin(seconds * 0.6));
    double jitter = 0.05 * Math.Sin(seconds * 7.3);
    return Math.Clamp(slow + jitter, 0.0, 1.0);
}

using var transport = new WinRtBleTransport();
using var glasses = new Glasses(transport);

try
{
    Console.WriteLine("Scanning for " + Protocol.DeviceName + "...");
    await glasses.ConnectAsync(TimeSpan.FromSeconds(15));
    Console.WriteLine("Connected.");

    // 1. Session guard: the device deep-sleeps when the 0xA4 timer expires.
    await glasses.SetDurationAsync(60);

    // 2. On-device smoothing so the lens glides between writes instead of
    //    stepping, and per-sample noise is absorbed without client-side
    //    filtering. Persisted; ignored by firmware < 4.15.7, so it is always
    //    safe to send. Rule of thumb: tau ~= 1-2x the write period.
    await glasses.SetLensSmoothingAsync(80);

    // 3. Safety envelope: the lens can never snap, even if this process streams
    //    garbage. Leave it off for minimum-latency discrete rewards.
    await glasses.SetLensMaxRateAsync(40);

    // 4. Fail clear on link loss, so a crash does not leave the wearer dark
    //    (fw >= 4.15.7). Bounded by the ~32 s supervision timeout, so the
    //    explicit clear before disconnect below still matters.
    await glasses.SetDisconnectBehaviorAsync(true);

    Console.WriteLine("Fast write (fw >= 4.16.3): " + (glasses.SupportsFastWrite ? "yes" : "no"));

    // 5. Open the stream. Push from any thread at any rate.
    FeedbackStream stream = glasses.StartFeedbackStream(30.0);
    Console.WriteLine($"Streaming at {stream.RateHz} Hz for 30 s.");
    Console.WriteLine("(In your app, call FeedReward() from your pipeline callback.)");

    DateTime started = DateTime.UtcNow;
    bool wasInCondition = false;

    while (true)
    {
        double seconds = (DateTime.UtcNow - started).TotalSeconds;
        if (seconds > 30.0)
        {
            break;
        }

        double value = SyntheticFeedback(seconds);

        // Proportional feedback: the tint tracks the signal continuously.
        stream.FeedReward(value);

        // Discrete reward: fire the instant a contingency is met. This preempts
        // the proportional stream and holds the reward tint briefly, so
        // reinforcement is not gated by the stream cadence.
        bool inCondition = value > 0.85;
        if (inCondition && !wasInCondition)
        {
            await stream.RewardEventAsync(duty: 0, holdMs: 300);
            Console.WriteLine($"  reward at t={seconds:F0} s");
        }

        wasInCondition = inCondition;

        // Your real loop is driven by your pipeline's callback, not a delay.
        await Task.Delay(20);
    }

    // StopAsync clears the lens by default: it otherwise freezes at the last
    // tint until session expiry.
    await stream.StopAsync();
    await glasses.DisconnectAsync();
    Console.WriteLine("Done.");
    return 0;
}
catch (DeviceNotFoundException)
{
    Console.Error.WriteLine(
        "No glasses found.\n" +
        "The radio powers down after 2 minutes with no client connected -\n" +
        "tap the magnet to the temple to wake them, then run this again.");
    return 1;
}
catch (GlassesException ex)
{
    Console.Error.WriteLine("Error: " + ex.Message);
    return 1;
}
