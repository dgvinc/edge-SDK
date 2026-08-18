# EDGE Glasses Python SDK — API Reference

**Firmware 4.15.6+ (lens-config methods need 4.15.7+; battery needs 4.16.1+) — current firmware 4.16.2, August 2026**
**SDK version:** 2.4.0

This document maps every Python SDK method to the exact bytes it writes over BLE.
For the full protocol (OTA, status/notify, PPG stream), see the
[BLE protocol deep-dive](../../docs/bluetooth-protocol.md).

---

## Connection

| Parameter | Value |
|-----------|-------|
| Advertised name | `Narbis_Edge` (exact match — the SDK filters on this) |
| Service UUID | `0x00FF` (`000000ff-0000-1000-8000-00805f9b34fb`) |
| Control characteristic | `0xFF01` (`0000ff01-0000-1000-8000-00805f9b34fb`) — read + write, all commands |
| Other characteristics | `0xFF02` OTA data, `0xFF03` status/notify, `0xFF04` PPG stream — out of SDK scope, see protocol doc |
| BLE stack | NimBLE; MTU 247; TX 0 dBm; no pairing/bonding |
| Write type | Write with response |
| Idle teardown | **2 minutes** with no client connected → full radio power-down; magnet tap re-arms advertising |
| Supervision timeout | 32 s |
| Simultaneous clients | **1** — advertising stops on connect; a second central cannot discover or connect while another client holds the link |
| NACKs | None — firmware silently clamps/drops bad args; the SDK validates client-side |

---

## The ≥ 2-byte write rule

A **1-byte write is the legacy opacity command** (0-255 → 0-100% static duty).
Therefore every opcode command must be at least 2 bytes — the SDK pads
argument-less opcodes to `[opcode, 0x00]`. Only `set_opacity()` intentionally
sends a single byte.

---

## Method → Wire Mapping

### Opacity (legacy single-byte write)

| Method | Wire bytes | Notes |
|--------|-----------|-------|
| `set_opacity(value)` | `[value]` (1 byte, 0-255) | 0-255 → 0-100% static duty; stops current mode; not persisted. Stream for real-time feedback — no 20 Hz ceiling (a stale doc figure); target 30-50 Hz with coalescing. See [Real-time lens control](#real-time-lens-control-feedback--operant-conditioning). |
| `clear()` | `[0x00]` | Fully transparent |
| `dark()` | `[0xFF]` | Fully opaque |

### Parameters

| Method | Wire bytes | Range (SDK clamps) | Persisted (NVS) |
|--------|-----------|--------------------|-----------------|
| `set_brightness(percent)` | `[0xA2, pct]` | 0-100% | yes |
| `set_duration(minutes)` | `[0xA4, min]` | 1-60 min (auto-sleep at end; default 30 min; persisted; timer runs from device wake — see protocol doc session-auto-sleep note) | yes |
| `set_static(duty)` | `[0xA5, duty]` | 0-100% | no |
| `set_strobe_frequency(hz)` | `[0xAB, hz]` | 1-50 Hz | yes |
| `set_strobe_duty(percent)` | `[0xAC, pct]` | 10-90% | yes |

### Lens config (firmware ≥ 4.15.7; older firmware ignores these)

| Method | Wire bytes | Range (SDK clamps) | Persisted (NVS) |
|--------|-----------|--------------------|-----------------|
| `set_lens_smoothing(ms)` | `[0xA0, ms // 10]` | 0-2550 ms (10 ms resolution); 0 = off — EMA glide between commanded static targets | yes |
| `set_lens_max_rate(percent_per_100ms)` | `[0xA1, rate]` | 0-100 %/100ms; 0 = unlimited — hard slew cap after the smoothing glide | yes |
| `set_disconnect_behavior(fail_clear)` | `[0xA3, 0x01/0x00]` | True = go clear on link loss, False = freeze at last output (default) | yes |

---

## Real-time lens control (feedback / operant conditioning)

Real-time lens dimming is the primary use case. The key facts:

### Update rate

There is **no 20 Hz protocol ceiling** — that figure was a stale conservative
doc value with no basis in the protocol. The firmware applies each commanded
duty on its 10 ms internal tick (100 Hz, no throttling); the **BLE link** is
the limit, and it depends on the negotiated connection parameters:

| Connection parameters | Sustained throughput |
|-----------------------|----------------------|
| Host accepts the glasses' defaults (20-30 ms interval, slave latency 1) | ~8-11 writes/sec (measured, fw 4.16.2) |
| Host requests throughput-optimized / low-latency params, held for the session | ~20 writes/sec (measured, median 31 ms/write, 20.8/sec) |
| Connection-event hard ceiling (~20-30 ms interval) | ~33-50/sec — reaching the full 30-50 Hz band uses firmware write-without-response on `0xFF01`, available on fw ≥ 4.16.3 (older firmware is write-with-response only) |

**Recommended operating band: 30-50 Hz as a configurable target**, with
coalescing on so the effective rate self-limits to your data rate (sources
typically produce ≤ 16-30 new values/sec, so coalesced writes rarely hit the
ceiling). Always **coalesce** (skip unchanged duty) and keep **exactly one
write in flight** — write-with-response paces throughput for you, so the
configured rate is a target, never a queue. Per-update latency: ~20-60 ms BLE
transport + <100 ms lens switch. `FeedbackStream` implements all of this
(default 30 Hz, cap 45 Hz).

> **Why not 12 / 20 Hz?** `12 Hz` was only the on-board breathe *pacer* cadence
> (the reference app's paced-breathing update rate); the `20 Hz` "ceiling" was
> a stale conservative figure. The real story is the table above.

### Clean static writes (firmware ≥ 4.16.2)

`set_static()` (`0xA5`) is a clean static-duty write that does **not** touch
`set_brightness()` (`0xA2`), the persistent max-tint / breathe depth. So you can
stream real-time dimming to 0 without zeroing the depth of the breathe / strobe
programs. On firmware ≤ 4.16.1 the two shared one variable (a `set_static()` to
0 left brightness at 0, so other programs rendered clear until `0xA2` was
re-sent); 4.16.2 decouples them and self-heals a persisted brightness of 0 at
boot.

### Smoothing to eliminate inter-write jitter (`0xA0`)

A live feedback stream is stepped (each write is a discrete jump) and a noisy
signal makes the lens visibly jitter. Set on-device EMA smoothing with
`set_lens_smoothing(ms)` **once at connect** so the lens **glides between your
writes** — continuous motion, no visible stepping, and it absorbs per-sample
noise without client-side filtering. Tune τ ≈ 1-2× your write period (30 Hz →
~33 ms → ~35-70 ms; ~80 ms is a good general value). Requires fw ≥ 4.15.8 for
continuous streams, ≥ 4.15.9 for full-resolution (10-bit PWM) smoothness. This
is the **recommended** way to get smooth real-time feedback without
over-sending. Optionally pair with `set_lens_max_rate()` (`0xA1`) as a hard
safety cap. `0xA0`/`0xA1` affect commanded static duty only, not strobe/breathe.

### Tint X → Y over Z seconds (timed fade)

Two ways:

1. **Firmware slew ramp — one write, on-device (recommended for a fixed fade).**
   Set the slew cap with `set_lens_max_rate()` (`0xA1`) so the firmware ramps at
   a constant rate, then write the target once with `set_static()` (`0xA5`).
   To move `Δduty` percent over `Z` seconds:
   `slew (%/100ms) = round(Δduty / (Z × 10))` (clamp 1-100; 0 = unlimited/instant).
   Example — fade fully clear→dark (Δ=100) over 2 s: `slew = 100/(2×10) = 5`:
   ```python
   await glasses.set_lens_max_rate(5)   # [0xA1, 5]
   await glasses.set_static(100)        # [0xA5, 100] -> lens ramps ~2 s on-device
   await glasses.set_lens_max_rate(0)   # [0xA1, 0] -> later writes snap again
   ```
   The slew is a fixed rate, so for an exact duration regardless of distance,
   recompute `slew` from the actual Δ each time. fw ≥ 4.15.7.
2. **Client-side interpolation (full curve control, any firmware).** Step
   `set_static()` from X to Y over Z seconds at your write rate:
   `steps = Z × rate`; `duty_i = round(X + (Y − X)·i/steps)`; write each at
   `1/rate` spacing (coalesced). Lets you do linear, ease-in/out, etc.

### Modes

| Method | Wire bytes | Notes |
|--------|-----------|-------|
| `start_strobe(hz=None, duty_pct=None)` | optional `[0xAB, hz]`, `[0xAC, pct]`, then `[0xA6, 0x00]` | 0xA6 arg is ignored (SDK sends 0). Omitted params keep stored values. |
| `start_breathe(bpm=..., inhale_pct=..., hold_top_ms=..., hold_bottom_ms=..., waveform=..., with_strobe=False)` | writes only the params given, then `[0xB0, arg]` | `arg` = `0x00` breathe / `0x01` breathe+strobe (fw ≥ 4.15.6) |
| `sync_breath(cycle_ms, inhale_pct=40)` | `[0xBA, cycle_lo, cycle_hi, inhale_pct]` | `cycle_ms` as u16 little-endian; 4 bytes total on wire. See boundary rule below. |

`start_breathe` parameter writes:

| Parameter | Wire bytes | Range (SDK clamps) | Persisted (NVS) |
|-----------|-----------|--------------------|-----------------|
| `bpm` | `[0xB1, bpm]` | 1-30 BPM (integer — use `sync_breath` for fractional rates) | yes |
| `inhale_pct` | `[0xB2, pct]` | 10-90% | yes |
| `hold_top_ms` | `[0xB3, ms // 100]` | 0-5000 ms → 0-50 units of 100 ms | yes |
| `hold_bottom_ms` | `[0xB4, ms // 100]` | 0-5000 ms → 0-50 units of 100 ms | yes |
| `waveform` | `[0xB5, w]` | `Waveform.SINE` = 0, `Waveform.LINEAR` = 1 | yes |

### Power / Maintenance

| Method | Wire bytes | Notes |
|--------|-----------|-------|
| `sleep()` | `[0xA7, 0x00]` | Deep sleep now; arg ignored (padded) |
| `factory_reset()` | `[0xBF, 0x00]` | Reset NVS settings; arg ignored (padded) |
| `send_command(opcode, payload=None)` | `[opcode, ...payload]` padded to ≥ 2 B | Low-level escape hatch |

### Battery (firmware ≥ 4.16.1, V1.2+ hardware)

| Method | Reads | Returns |
|--------|-------|---------|
| `get_battery()` | Battery Level `0x2A19` (GATT read) on the standard Battery Service `0x180F` | `int` 0-100, or `None` if the `0x180F` service is absent (pre-4.16.1 firmware, or a board built without the battery divider) |

`get_battery()` reads the standard BLE Battery Service — a GATT read on `0x180F`
/ `0x2A19`, not an opcode on the control characteristic. On fw ≥ 4.16.1 the
`0x2A19` characteristic is registered on every unit but **reads 0 while the
level is unknown** — it cannot distinguish "unknown" from a genuine 0%. For
millivolts + a charging flag + a real unknown flag, read the `0xFB` status frame
on `0xFF03`: `[mv:u16 LE][soc:u8][charging:u8]`, where `soc = 0xFF` means
unknown/unsupported. Opcode `0xC7` polls the battery / dumps probe diagnostics
(`[0xC7, 0x00]` refresh, `[0xC7, 0x01]` reprobe the ADC channel). A board without
the divider reports unsupported there (mv=0, soc=0xFF, charging=0xFF) — treat a
missing `0x180F` service or a persistent 0 as "battery not available on this
unit." See the [protocol doc](../../docs/bluetooth-protocol.md) for the full
surface.

### Preset Sessions (fixed-parameter)

The firmware no longer ramps any parameter over the session; presets just
configure the renderer and set the auto-sleep duration.

| Method | Sequence sent |
|--------|---------------|
| `session_relax(duration=10)` | `[0xA2, 100]`, `[0xB1, 5]`, `[0xB5, 0]`, `[0xB0, 0]`, `[0xA4, min]` — 5 BPM sine, brightness 100 |
| `session_meditate(duration=10)` | `[0xB1, 6]`, `[0xB5, 0]`, `[0xB0, 0]`, `[0xA4, min]` — 6 BPM sine (device default) |
| `session_focus(duration=10)` | `[0xAB, 12]`, `[0xB1, 8]`, `[0xB0, 1]`, `[0xA4, min]` — breathe+strobe, 12 Hz, 8 BPM |
| `session_sleep(duration=15)` | `[0xB1, 4]`, `[0xB5, 0]`, `[0xB0, 0]`, `[0xA4, min]` — 4 BPM sine |

---

## 0xBA Breathe Sync — boundary rule

`sync_breath(cycle_ms, inhale_pct=40)` (firmware ≥ 4.15.5):

- Restarts the breathe cosine **at the instant of the write** and sets the EXACT
  cycle length in milliseconds — this is how you get fractional breathing rates
  (`0xB1` is integer-BPM only).
- **Send only at the breath-cycle boundary (inhale onset), never mid-breath** —
  the waveform restarts immediately on receipt.
- Auto-expires **2 cycles** after the last sync, reverting to the stored
  integer-BPM rate — re-send once per breath to stay locked.
- Ignored by older firmware; always safe to send.

---

## Command Summary

| Opcode | Name | Arg | Persisted (NVS) | SDK method |
|--------|------|-----|-----------------|------------|
| *(1 byte)* | Legacy opacity | 0-255 → 0-100% static duty; stops current mode | no | `set_opacity` |
| `0xA0` | Lens smoothing | EMA τ ×10 ms (0 = off); fw ≥ 4.15.7 | yes | `set_lens_smoothing` |
| `0xA1` | Lens max transition rate | 0-100 %/100ms (0 = unlimited); fw ≥ 4.15.7 | yes | `set_lens_max_rate` |
| `0xA2` | Brightness | 0-100% | yes | `set_brightness` |
| `0xA3` | On-disconnect behavior | 0 continue / 1 fail clear; fw ≥ 4.15.7 | yes | `set_disconnect_behavior` |
| `0xA4` | Session duration | 1-60 min (auto-sleep at end; default 30 min; timer runs from device wake — see protocol doc session-auto-sleep note) | yes | `set_duration` |
| `0xA5` | Static mode + duty | 0-100% | no | `set_static` |
| `0xA6` | Start strobe mode | arg ignored (send 0) | no | `start_strobe` |
| `0xA7` | Sleep now | arg ignored (send 0) | no | `sleep` |
| `0xAB` | Strobe frequency | 1-50 Hz | yes | `set_strobe_frequency` |
| `0xAC` | Strobe duty | 10-90% | yes | `set_strobe_duty` |
| `0xB0` | Start breathe mode | `0x00` breathe / `0x01` breathe+strobe (fw ≥ 4.15.6) | no | `start_breathe` |
| `0xB1` | Breathe rate | 1-30 BPM (integer) | yes | `start_breathe(bpm=...)` |
| `0xB2` | Breathe inhale ratio | 10-90% | yes | `start_breathe(inhale_pct=...)` |
| `0xB3` | Breathe hold-top | 0-50 (×100 ms) | yes | `start_breathe(hold_top_ms=...)` |
| `0xB4` | Breathe hold-bottom | 0-50 (×100 ms) | yes | `start_breathe(hold_bottom_ms=...)` |
| `0xB5` | Breathe waveform | 0 sine / 1 linear | yes | `start_breathe(waveform=...)` |
| `0xBA` | Breathe sync | `[cycle_ms:u16 LE][inhale_pct:u8]` | no | `sync_breath` |
| `0xBF` | Factory reset | arg ignored (send 0) | — | `factory_reset` |
| `0xC7` | Battery poll / probe diagnostics | `0x00` refresh / `0x01` reprobe ADC; fw ≥ 4.16.1 | — | not an SDK method — use `get_battery()` (0x180F) / see [protocol doc](../../docs/bluetooth-protocol.md) |
| `0xA8`/`0xA9`/`0xAA`/`0xAD` | OTA | — | — | not an SDK method — see [protocol doc](../../docs/bluetooth-protocol.md) |

---

## Lens physics note

Duty 1-100% maps to raw 265-1023 — a perceptual floor so 1% is already visible
(firmware ≥ 4.15.4). Duty 0 is fully clear.

---

## Legacy / unused opcodes

`0xB6` pulse-on-beat, `0xB7` PPG program 0-3, `0xB8` coherence difficulty,
`0xB9` adaptive pacer, `0xCA` external-IBI injection, `0xCB` HR source,
`0xD0` detector reset, `0xE0` coherence tuning — the on-board coherence pipeline.
Functional but unused: all processing is app-side now. The Edge↔earclip BLE relay
is compile-disabled on stock builds. See
[protocol doc §4.8](../../docs/bluetooth-protocol.md#48-legacy-on-board-coherence-pipeline-unused)
for the full story. The SDK does not expose these.
