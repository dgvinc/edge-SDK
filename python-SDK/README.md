# EDGE Glasses Python SDK

Control EDGE Smart LCD Glasses (`Narbis_Edge`) over Bluetooth Low Energy.

**v2.4.0** — targets glasses firmware 4.15.6+ (lens-config needs 4.15.7+; battery needs 4.16.1+). See the
[migration table](#migrating-from-v1) below.

The glasses are a **display**: all coherence / HRV / biofeedback processing runs in your
app. You compute your signal, then drive the lens by commanding the firmware's
breathe / static / strobe renderer.

## Installation

```bash
pip install edge-glasses
```

## Quick Start

**Connect and set opacity — the simplest thing that works.** `set_static(duty)` sets the lens **0 (clear) → 100 (fully dark)**:

```python
import asyncio
from edge_glasses import Glasses

async def main():
    async with Glasses() as glasses:      # scans for 'Narbis_Edge' and connects
        await glasses.set_duration(60)    # session guard: no auto-sleep for 60 min
        await glasses.set_static(0)       # 0   = clear
        await glasses.set_static(100)     # 100 = fully dark
        await glasses.set_static(50)      # 50  = half

asyncio.run(main())
```

Prefer raw BLE? Every command is a ≥ 2-byte **write-with-response** on characteristic `0xFF01` (a 1-byte write is the legacy opacity command):

```python
import asyncio
from bleak import BleakClient, BleakScanner

CTRL = "0000ff01-0000-1000-8000-00805f9b34fb"  # control characteristic 0xFF01

async def main():
    dev = await BleakScanner.find_device_by_name("Narbis_Edge")
    async with BleakClient(dev) as c:
        await c.write_gatt_char(CTRL, bytes([0xA4, 60]), response=True)  # session guard
        await c.write_gatt_char(CTRL, bytes([0xA5, 50]), response=True)  # 0 clear .. 100 dark

asyncio.run(main())
```

**Real-time opacity streaming — a drop-in screen-dimmer replacement.** Hold one connection open and push your feedback value into a `FeedbackStream`: `feed(duty)` takes the same 0 (clear) → 100 (fully dark) your on-screen dimmer already produces, `feed_reward(v)` takes a 0..1 reward value. Call them from any callback at any rate — the stream's internal writer handles the ~30 Hz decimation, unchanged-value coalescing, and write serialization for you.

```python
import asyncio
from edge_glasses import Glasses

async def main():
    # The core pattern: a wearable screen dimmer. Map ANY protocol's feedback
    # value to lens tint -- dim when out of condition, clear when in.
    async with Glasses() as glasses:
        await glasses.set_duration(60)                 # session guard: no auto-sleep for 60 min
        stream = glasses.start_feedback_stream()       # 30 Hz writer -- coalesces + serializes for you
        your_pipeline.on_update(stream.feed_reward)    # push your 0..1 feedback value, any callback, any rate
        # or stream.feed(duty) with your dimmer's existing 0-100% value
        await asyncio.Event().wait()                   # run until you end the session

    # Paced breathing, when the protocol calls for it:
    #   await glasses.start_breathe(bpm=6)

asyncio.run(main())
```

## Features

- Simple opacity control (0-255, streamable — target 30–50 Hz, coalesced; no 12/20 Hz limit)
- On-board breathe engine (rate, inhale ratio, holds, waveform, optional strobe modulation)
- Breath phase-lock (`sync_breath`) for app-paced, fractional-rate breathing
- Strobe mode (1-50 Hz, 10-90% duty)
- Lens-config knobs (fw 4.15.7+): on-device smoothing so streamed feedback glides, a transition-rate safety cap, and go-clear-on-disconnect
- Glasses battery read over BLE (fw 4.16.1+ on V1.2 hardware): standard Battery Service via `get_battery()`
- Fixed-parameter preset sessions (relax, focus, meditate, sleep)
- Async/await API using `bleak` BLE library
- Cross-platform (Windows, macOS, Linux, Raspberry Pi)

## Connection Quirks

- **Advertised name is exactly `Narbis_Edge`** — the SDK filters on the exact name.
- **2-minute advertising teardown:** after 2 minutes with no client connected the
  glasses fully power down the radio. If a scan can't find them, tap the magnet to
  the temple briefly to wake them and re-arm advertising.
- **No NACKs:** the firmware never rejects a command — bad arguments are silently
  clamped or dropped on the device. The SDK clamps everything client-side.
- **Every opcode write is ≥ 2 bytes** — a 1-byte write is the legacy opacity command.
  The SDK handles this automatically (argument-less opcodes are padded).

## Standalone Programs (no app needed)

The glasses run sensor-free programs on their own, cycled by a short magnet tap
(0.15-4 s) on the temple:

| Program | Behavior |
|---------|----------|
| 1 — BREATHE | 6 BPM sine, lens tint follows the waveform (boot default) |
| 2 — BREATHE+STROBE | 10 Hz strobe, dark-phase duty modulated by the breathing waveform |
| 3 — STROBE | plain 10 Hz strobe |

A long magnet close (≥ 5 s) is deep sleep. On a program change the lens shows N slow
fade-dark pulses as an indicator.

## Integrations

Works natively with OpenBCI, brainflow, LSL, and any BLE pipeline.

| Platform | Example | Description |
|----------|---------|-------------|
| **Any protocol** | `examples/screen_dimmer.py` | **Wearable screen dimmer** — tint from any 0..1 feedback value (threshold or proportional) |
| **OpenBCI** | `examples/openbci_feedback.py` | EEG neurofeedback via brainflow |
| **Evoked potentials** | `examples/evoked_potential.py` | Lens as SSVEP / VEP / P300 visual stimulator |
| **Muse** | `examples/muse_eeg.py` | Meditation/focus training |
| **Polar** | `examples/polar_hrv.py` | HRV coherence training |
| **LSL** | `examples/lsl_integration.py` | Lab Streaming Layer bridge |
| **HRV** | `examples/hrv_breathing.py` | Heart rate variability training |

See [docs/INTEGRATION_GUIDE.md](docs/INTEGRATION_GUIDE.md) for complete integration
documentation.

## Usage

### Basic Control

```python
from edge_glasses import Glasses
import asyncio

async def main():
    glasses = Glasses()
    await glasses.connect()

    # Simple opacity control (legacy 1-byte write; stops the current mode)
    await glasses.clear()           # Fully transparent
    await glasses.set_opacity(128)  # 50% dark
    await glasses.dark()            # Fully opaque

    # Static mode at a specific duty cycle
    await glasses.set_static(75)    # Hold at 75%

    # Sleep the device
    await glasses.sleep()

    await glasses.disconnect()

asyncio.run(main())
```

### Guided Breathing

```python
from edge_glasses import Glasses, Waveform

async def breathing():
    async with Glasses() as glasses:
        # 5 BPM sine breathing with a 1 s hold at full dark
        await glasses.start_breathe(
            bpm=5,
            inhale_pct=40,
            hold_top_ms=1000,
            waveform=Waveform.SINE
        )

        # Or breathe+strobe: strobe duty modulated by the breath waveform
        await glasses.start_breathe(bpm=8, with_strobe=True)
```

### App-paced Breathing (phase lock)

For fractional rates or breathing paced by your app, phase-lock the on-board
engine with `sync_breath` — send it once per breath, exactly at the inhale onset:

```python
async def paced_breathing():
    async with Glasses() as glasses:
        await glasses.start_breathe()          # start with stored settings
        while True:
            await glasses.sync_breath(5500)    # 5.5 s cycle = 10.9 BPM
            await asyncio.sleep(5.5)           # next boundary
```

The sync auto-expires 2 cycles after the last write (falls back to the stored
integer-BPM rate), and older firmware ignores it — safe to send unconditionally.

### Strobe

```python
async def strobe():
    async with Glasses() as glasses:
        await glasses.start_strobe(hz=10, duty_pct=50)
```

### Preset Sessions

Presets are fixed-parameter: the firmware no longer ramps anything over the
session. Each preset configures the renderer and sets the auto-sleep duration.

```python
async def presets():
    async with Glasses() as glasses:
        await glasses.session_relax(duration=15)     # 5 BPM sine, full brightness
        await glasses.session_meditate(duration=10)  # 6 BPM sine (device default)
        await glasses.session_focus(duration=10)     # breathe+strobe, 12 Hz, 8 BPM
        await glasses.session_sleep(duration=20)     # 4 BPM sine, auto-sleeps after
```

### Scanning for Devices

```python
async def find_devices():
    # Scan for EDGE Glasses (exact name 'Narbis_Edge')
    devices = await Glasses.scan(timeout=5.0)

    for device in devices:
        print(f"{device.name} - {device.address} (RSSI: {device.rssi})")

    # Connect to specific device
    if devices:
        glasses = Glasses(address=devices[0].address)
        await glasses.connect()
```

### Real-time Control (Research/Neurofeedback)

Map a continuous signal to opacity. There is **no 12 Hz or 20 Hz limit** — the old "12 Hz"
was the on-board breathe pacer and the "20 Hz ceiling" was a stale conservative figure; the
real limit is the BLE link (a host on default connection parameters sustains ~8–11 writes/sec,
one requesting throughput-optimized parameters ~20 writes/sec). Prefer `start_feedback_stream()`
(default 30 Hz, coalesced, writes serialized for you), but a hand loop works too:

```python
async def neurofeedback_loop():
    """Example: Control glasses based on external data"""
    async with Glasses() as glasses:
        while True:
            # Get data from EEG, HRV sensor, etc.
            alpha_power = get_eeg_alpha()  # Your function

            # Map to opacity (higher alpha = darker/calmer)
            opacity = int(alpha_power * 255)
            await glasses.set_opacity(opacity)

            await asyncio.sleep(1/30)  # ~30 Hz update rate
```

**Smoothing.** A raw stream is stepped and a noisy signal makes the lens jitter. Enable
on-device EMA smoothing once at connect and the lens **glides between your writes** and
absorbs per-sample noise — the recommended way to get smooth feedback without over-sending.
Tune τ ≈ 1–2× your write period (30 Hz → ~33 ms → `set_lens_smoothing(80)` is a good general value):

```python
await glasses.set_lens_smoothing(80)   # ~80 ms EMA glide (fw 4.15.8+ streaming, 4.15.9+ full-res)
```

**Timed tint ramp (X → Y over Z s).** For a fixed on-device fade, set the slew cap then write
the target once: `rate = round(Δduty / (Z × 10))` %/100ms. Fade clear → dark over 2 s
(Δ = 100 → rate 5):

```python
await glasses.set_lens_max_rate(5)     # 5 %/100ms
await glasses.set_static(100)          # ramps over ~2 s on-device
await glasses.set_lens_max_rate(0)     # afterward: writes snap again
```

For breathing entrainment, do **not** stream opacity to draw a waveform — use the
on-board breathe engine and phase-lock it with `sync_breath()` at breath boundaries.

## API Reference

Full method → wire-byte mapping in [docs/API_REFERENCE.md](docs/API_REFERENCE.md).

### Connection

| Method | Description |
|--------|-------------|
| `Glasses(address=None)` | Create controller. Auto-scans if no address. |
| `await glasses.connect()` | Connect (can't find it? tap the magnet) |
| `await glasses.disconnect()` | Disconnect from device |
| `await Glasses.scan(timeout=5.0)` | Scan for `Narbis_Edge` devices |

### Simple Control

| Method | Description |
|--------|-------------|
| `await glasses.set_opacity(0-255)` | Set lens opacity (legacy 1-byte write) |
| `await glasses.clear()` | Fully transparent |
| `await glasses.dark()` | Fully opaque |
| `await glasses.set_static(0-100)` | Static mode at duty % (fw ≥ 4.16.2: clean static — no longer touches `set_brightness`) |
| `glasses.start_feedback_stream(rate_hz=30)` | Plug-and-play real-time stream: returns a `FeedbackStream` — `feed(duty)` / `feed_reward(0..1)` for proportional dimming; `await reward_event(duty, hold_ms)` for immediate discrete operant rewards (bypasses the tick); internal writer coalesces + serializes; `await stream.stop()` clears the lens. Default 30 Hz, capped at 45 Hz. On fw ≥ 4.16.3 the writer auto-uses write-without-response for higher throughput (`supports_fast_write`) |
| `await glasses.get_battery()` | Battery level 0-100, or `None` if unavailable (fw ≥ 4.16.1 + V1.2 hardware; reads `0x2A19`). A returned `0` may mean *unknown* on some builds — the `0xFB` status frame carries a true unknown/charging flag |
| `await glasses.set_brightness(0-100)` | Lens level / breathe depth (persisted; the max-tint that scales breathe & strobe — on fw ≥ 4.16.2 `set_static` no longer writes it, on fw ≤ 4.16.1 they shared one variable) |
| `await glasses.set_lens_smoothing(ms)` | On-device glide between streamed targets, EMA τ 0-2550 ms, 0 = off (persisted; fw 4.15.7+) |
| `await glasses.set_lens_max_rate(pct)` | Hard cap on lens transition speed, %/100ms, 0 = unlimited (persisted; fw 4.15.7+) |
| `await glasses.set_disconnect_behavior(fail_clear)` | True = lens goes clear on link loss instead of freezing (persisted; fw 4.15.7+) |
| `await glasses.sleep()` | Enter deep sleep |
| `await glasses.factory_reset()` | Reset persisted settings |

### Breathe / Strobe

| Method | Description |
|--------|-------------|
| `await glasses.start_breathe(bpm=..., inhale_pct=..., hold_top_ms=..., hold_bottom_ms=..., waveform=..., with_strobe=False)` | Configure + start breathe mode |
| `await glasses.sync_breath(cycle_ms, inhale_pct=40)` | Phase-lock breathe (send at breath boundary only) |
| `await glasses.start_strobe(hz=None, duty_pct=None)` | Configure + start strobe mode |
| `await glasses.set_strobe_frequency(1-50)` | Strobe Hz (persisted) |
| `await glasses.set_strobe_duty(10-90)` | Strobe duty % (persisted) |
| `await glasses.set_duration(1-60)` | Auto-sleep session length (persisted) |
| `await glasses.send_command(opcode, payload=None)` | Low-level opcode write (padded to ≥ 2 B) |

### Preset Sessions

| Method | Description |
|--------|-------------|
| `await glasses.session_relax(duration)` | 5 BPM sine, brightness 100 |
| `await glasses.session_meditate(duration)` | 6 BPM sine (device default) |
| `await glasses.session_focus(duration)` | Breathe+strobe, 12 Hz, 8 BPM |
| `await glasses.session_sleep(duration)` | 4 BPM sine, auto-sleep after |

## Migrating from v1

| v1 | v2 |
|----|----|
| `set_strobe(start_hz, end_hz)` | Removed — no in-session frequency ramp. Use `set_strobe_frequency(hz)` / `start_strobe(hz=...)`. |
| `set_breathing(inhale, hold_in, exhale, hold_out)` | Removed — use `start_breathe(bpm=..., inhale_pct=..., hold_top_ms=..., hold_bottom_ms=...)`. |
| `resume()` | Removed — `[0xA6]` now means "start strobe". Use `start_strobe()`. |
| `start_session(config...)` | Removed — use a `session_*` preset or explicit `start_breathe()` / `start_strobe()` + `set_duration()`. |
| `hold(duty)` | Renamed `set_static(duty)`. |
| `set_opacity()`, `clear()`, `dark()` | Unchanged. |
| `set_brightness()`, `set_duration()`, `sleep()` | Unchanged signatures (duration no longer restarts a ramping session). |
| Device name `Smart_Glasses` | Now `Narbis_Edge` (exact match). |

## Legacy On-board Coherence

Earlier firmware ran a full coherence/PPG biofeedback pipeline on the glasses
themselves (pulse-on-beat, PPG programs, coherence difficulty, adaptive pacer, and
related opcodes). Those opcodes still exist and function, but they are legacy — no
longer used by Narbis apps, which compute everything app-side and drive the lens
directly. Don't build on them; see the
[protocol deep-dive](../docs/bluetooth-protocol.md)
if you need the full story.

## Troubleshooting

### Device not found
- The glasses stop advertising after **2 minutes** with no client connected —
  tap the magnet to wake them, then rescan
- Check Bluetooth is enabled
- Move closer to device
- Try `await Glasses.scan()` to verify detection (name must be exactly `Narbis_Edge`)

### Connection fails
- Disconnect from other apps (nRF Connect, the Narbis app, etc.)
- Restart Bluetooth
- Check battery level on glasses

### Commands not working
- Verify connection with `glasses.is_connected`
- Remember: the firmware never NACKs — a "successful" write with an out-of-range
  argument is silently clamped or dropped on the device
- Check for exceptions in your async code
- Test with simple `set_opacity(255)` first

### Linux permissions
```bash
sudo setcap 'cap_net_raw,cap_net_admin+eip' $(readlink -f $(which python3))
```

Or run with sudo, or add user to `bluetooth` group.

## License

MIT License - see LICENSE file.

## Links

- [API Reference](docs/API_REFERENCE.md) — Python method → wire-byte mapping
- [Integration Guide](docs/INTEGRATION_GUIDE.md) — OpenBCI, Muse, Polar, LSL
- [BLE Protocol Deep-dive](../docs/bluetooth-protocol.md) — full protocol, OTA, status/PPG characteristics
