# EDGE Glasses — C# / .NET SDK

Control Narbis EDGE smart glasses from C#. Same API surface as the
[Python](../python-SDK/) and [JavaScript](../js-SDK/) SDKs, and byte-for-byte
identical on the wire — enforced by a
[cross-language golden-vector test](#cross-language-verification).

| Target | Bluetooth |
|---|---|
| `net8.0-windows10.0.19041.0` | Built-in `WinRtBleTransport` |
| `net472` | Core only; bring your own stack, or opt in with `-p:EdgeEnableWinRt=true` |
| `netstandard2.0` | Core only — consumable from .NET Framework 4.6.1+, .NET Core, Mono, Unity |

No pairing or bonding is required, and the device does not need to be paired in
Windows Settings.

---

## Installing

Not yet published to NuGet. Reference the project directly:

```bash
dotnet add reference path/to/edge-SDK/csharp-SDK/src/Narbis.EdgeGlasses/Narbis.EdgeGlasses.csproj
```

Or build a package locally and consume it from a local feed:

```bash
dotnet pack csharp-SDK/src/Narbis.EdgeGlasses -c Release
```

For a .NET Framework project without the SDK-style csproj, build the `net472`
target and reference the resulting `Narbis.EdgeGlasses.dll`.

---

## Quick start

```csharp
using Narbis.EdgeGlasses;

using var transport = new WinRtBleTransport();
using var glasses = new Glasses(transport);

await glasses.ConnectAsync();          // scans for "Narbis_Edge"
await glasses.SetDurationAsync(60);    // session guard: no auto-sleep for 60 min
await glasses.SetStaticAsync(50);      // 0 = clear .. 100 = fully dark
await glasses.ClearAsync();            // never leave the wearer dark
await glasses.DisconnectAsync();
```

Scanning first, if you want to show the user a device list:

```csharp
IReadOnlyList<ScanResult> found = await WinRtBleTransport.ScanAsync(TimeSpan.FromSeconds(8));
if (found.Count == 0)
{
    // The radio powers down after 2 minutes idle — ask the user to tap
    // the magnet to the temple, then rescan.
}
using var transport = new WinRtBleTransport(found[0].Address);
```

---

## The core integration: a wearable screen dimmer

Classic neurofeedback dims the training display when the trainee falls out of
condition and clears it when they are in condition. The Edge does the same on
the lens itself, so it drops into **any protocol** (SMR, alpha/theta, HEG, EMG
down-training, HRV…) wherever your software already produces a feedback value.

`FeedbackStream` ships that whole contract — decimation, coalescing, write
serialization — so you never hand-roll a BLE cadence loop:

```csharp
FeedbackStream stream = glasses.StartFeedbackStream(30.0);   // ~30 Hz writer

// From your pipeline, any thread, any rate:
myPipeline.OnUpdate += v => stream.FeedReward(v);   // 0..1, 1 = in condition
// or with your dimmer's existing 0-100 % scale:
stream.Feed(duty);

// The instant a contingency is met — bypasses the tick entirely:
await stream.RewardEventAsync(duty: 0, holdMs: 150);

await stream.StopAsync();     // stops the writer and clears the lens
```

`Feed` is lock-free and allocation-free: it is safe to call straight from a DSP
or BLE-notification callback at any rate. Only changed values reach the wire.

### Proportional vs. discrete reward

These have different latency needs, and only the first is governed by the
streaming cadence.

- **Proportional** (`Feed` / `FeedReward`) — the tint tracks a continuously
  varying signal. A 30 Hz stream gives ~33 ms granularity, far below the
  upstream EEG analysis window (typically 250 ms – 1 s) that dominates the loop.
- **Discrete** (`RewardEventAsync`) — reinforcement the instant a contingency is
  met. This writes immediately instead of waiting up to one stream period, so
  latency is ~20–60 ms transport + ~40–50 ms lens switch. It preempts the
  proportional stream, waiting at most one in-flight write.

### Smooth motion off a noisy signal

Send `SetLensSmoothingAsync(80)` **once at connect**. The firmware then glides
between your commanded targets with an ~80 ms EMA, so the lens moves
continuously between writes instead of stepping, and per-sample noise is
absorbed on-device rather than in your filter chain. It is persisted, and older
firmware ignores it, so it is always safe to send.

Pair it with `SetLensMaxRateAsync(40)` as a hard safety envelope: the lens then
cannot snap even if a host streams garbage. Leave both off when you want
minimum-latency discrete rewards.

---

## Using your own Bluetooth stack

If your application already has BLE code, you do not have to replace it. Hand
the SDK two callbacks and keep the protocol encoding, clamping, write
serialization and the feedback stream:

```csharp
var transport = new CallbackTransport(
    writeAsync: (frame, withResponse, ct) => MyGattWriteAsync(frame, withResponse, ct),
    isConnected: () => myLink.IsUp);

// Firmware >= 4.16.3 advertises write-without-response on 0xFF01. Declare it
// after checking the characteristic's properties; off is always safe.
transport.SupportsWriteWithoutResponse = false;

using var glasses = new Glasses(transport);
await glasses.SetStaticAsync(50);
```

Or skip the SDK objects entirely and use the encoder alone — `Protocol` is pure,
static, and has no Bluetooth dependency:

```csharp
byte[] frame = Protocol.SetStatic(50);        // A5 32
await myCharacteristic.WriteValueAsync(frame);
```

That path is available on every target framework, including `netstandard2.0`.

---

## API reference

### Connection

| Member | Notes |
|---|---|
| `new Glasses(IGlassesTransport, bool ownsTransport = true)` | |
| `ConnectAsync(TimeSpan?, CancellationToken)` | Throws `DeviceNotFoundException` when nothing advertises as `Narbis_Edge`. |
| `DisconnectAsync()` | The lens **freezes** at its last tint unless fail-clear is enabled. |
| `IsConnected` | |
| `SupportsFastWrite` | True on firmware ≥ 4.16.3. |
| `WinRtBleTransport.ScanAsync(TimeSpan?, CancellationToken)` | Static; devices strongest-signal first. |

### Lens control

| Method | Wire | Range |
|---|---|---|
| `SetOpacityAsync(v)` | `[v]` — **1 byte** | 0–255 |
| `ClearAsync()` / `DarkAsync()` | `[0x00]` / `[0xFF]` | |
| `SetStaticAsync(duty)` | `[0xA5, duty]` | 0–100 % |
| `SetBrightnessAsync(pct)` | `[0xA2, pct]` | 0–100 %, persisted |
| `SetDurationAsync(min)` | `[0xA4, min]` | 1–60 min, persisted |
| `SetLensSmoothingAsync(ms)` | `[0xA0, ms/10]` | 0–2550 ms, persisted, fw ≥ 4.15.7 |
| `SetLensMaxRateAsync(r)` | `[0xA1, r]` | 0–100 %/100 ms, persisted, fw ≥ 4.15.7 |
| `SetDisconnectBehaviorAsync(b)` | `[0xA3, 0\|1]` | persisted, fw ≥ 4.15.7 |

### Modes

| Method | Wire |
|---|---|
| `StartStrobeAsync(hz?, dutyPct?)` | `[0xAB…]`, `[0xAC…]`, `[0xA6, 0x00]` |
| `SetStrobeFrequencyAsync(hz)` | `[0xAB, hz]`, 1–50 Hz |
| `SetStrobeDutyAsync(pct)` | `[0xAC, pct]`, 10–90 % |
| `StartBreatheAsync(BreatheOptions)` | `[0xB1…0xB5]` then `[0xB0, 0\|1]` |
| `SyncBreathAsync(cycleMs, inhalePct)` | `[0xBA, lo, hi, inhale]`, fw ≥ 4.15.5 |

### Power, presets, low level

| Method | |
|---|---|
| `SleepAsync()` / `FactoryResetAsync()` | |
| `GetBatteryAsync()` → `int?` | null when the unit exposes no `0x180F` service |
| `SessionRelaxAsync/MeditateAsync/FocusAsync/SleepAsync(min)` | fixed-parameter presets |
| `SendCommandAsync(opcode, params byte[])` | pads to the 2-byte minimum |
| `Protocol.*` | pure frame builders, no BLE |

### FeedbackStream

| Member | |
|---|---|
| `Glasses.StartFeedbackStream(rateHz = 30)` | rate clamped 1–45 Hz |
| `Feed(duty)` | 0–100, proportional, lock-free |
| `FeedReward(value)` | 0..1, 1 = in condition |
| `RewardEventAsync(duty, holdMs)` | discrete, immediate, preempts the stream |
| `StopAsync(clearLens = true)` | idempotent; clears the lens by default |

### Exceptions

All derive from `GlassesException`, so one `catch` covers the SDK.

| Type | Meaning |
|---|---|
| `DeviceNotFoundException` | Nothing advertising as `Narbis_Edge` — magnet tap needed. |
| `GlassesConnectionException` | Not connected, or the connect failed. |
| `CommandException` | The GATT write did not reach the device. Never means the firmware rejected an argument — it never NACKs. |
| `GlassesTimeoutException` | The operation exceeded its timeout. |

---

## Things that will bite you

Every one of these is enforced or handled by the SDK, but they matter when you
read the wire trace.

- **Every command is ≥ 2 bytes.** A 1-byte write is the *legacy opacity
  command*, so a bare `[0xA6]` is read as "opacity 166", not "start strobe".
  All builders pad argument-less opcodes to `[opcode, 0x00]`.
  `SetOpacityAsync` is the one deliberate exception.
- **The firmware never NACKs.** Out-of-range arguments are silently clamped or
  dropped on the device, so a bad value fails *quietly*. Every method clamps
  client-side, which is what makes "what you send is what runs" true.
- **Writes must never overlap.** Concurrent GATT writes to `0xFF01` fail
  outright on WinRT. `Glasses` serializes every write on an internal semaphore,
  so your `Task`s and the feedback stream cannot collide.
- **The glasses sleep on a timer.** Default 30 minutes from device *wake*, and
  `SetDurationAsync` changes the total but does not restart the clock. Set it at
  session start.
- **The radio powers down after 2 minutes idle.** If a scan finds nothing, the
  user must tap the magnet on the temple. Surface that in your UX instead of
  retrying forever.
- **The lens freezes on disconnect.** A crashed app leaves the last tint in
  place until session expiry. Call `ClearAsync()` before an intentional
  disconnect, and consider `SetDisconnectBehaviorAsync(true)` as a backstop.
- **A magnet tap overrides your app.** Gestures stay live while connected, so a
  tap mid-session cycles the standalone program out from under you.

Full detail: [protocol reference](../docs/bluetooth-protocol.md).

---

## WinForms / WPF note

Every SDK call is `async`. From a UI event handler, `await` it as usual — never
`.Result` or `.Wait()`, which will deadlock on the UI `SynchronizationContext`:

```csharp
private async void OnStartClicked(object sender, EventArgs e)
{
    await _glasses.ConnectAsync();
    await _glasses.SetDurationAsync(60);
}
```

The SDK uses `ConfigureAwait(false)` internally, so its continuations never
marshal back to the UI thread.

---

## Building and testing

```bash
cd csharp-SDK
dotnet build   Narbis.EdgeGlasses.sln -c Release
dotnet test    tests/Narbis.EdgeGlasses.Tests -c Release
```

The library builds warnings-clean with `TreatWarningsAsErrors`.

144 tests cover frame encoding, clamping at every boundary, the ≥ 2-byte rule,
command sequences, presets, error paths, the fast-write decision, write
serialization under concurrency, `FeedbackStream` behaviour (coalescing, reward
preemption, hold windows, retry-after-failure, multi-threaded feeds), and
`WinRtBleTransport`'s address parsing.

### Cross-language verification

`tools/golden/gen_golden.py` drives the **reference Python SDK** through a fixed
script of 70 calls behind a fake BLE client and records all 95 resulting frames
to `tools/golden/golden_frames.txt`. This suite replays the same script and
asserts byte-for-byte equality, including the write-with/without-response
decision. The C++ SDK runs the identical check.

So a divergence in clamping, padding, or byte order between any two language
bindings fails a build rather than reaching a customer's glasses.

```bash
python tools/golden/gen_golden.py --check   # verify the vectors are current
```

---

## Examples

| Project | Shows |
|---|---|
| [`QuickStart`](examples/QuickStart) | Scan, connect, session guard, battery, step the tint. |
| [`ScreenDimmer`](examples/ScreenDimmer) | The biofeedback pattern: smoothing, slew cap, fail-clear, proportional stream, discrete rewards. |
| [`BringYourOwnBle`](examples/BringYourOwnBle) | `Protocol` and `CallbackTransport` with no Bluetooth stack — **runs with no hardware**. |

```bash
dotnet run --project csharp-SDK/examples/BringYourOwnBle
```

---

## License

MIT — see [LICENSE](LICENSE).
