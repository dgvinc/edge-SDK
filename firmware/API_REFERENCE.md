# EDGE Glasses BLE API Reference

**Firmware Version:** 4.16.3 (current) — per-feature minimum versions are noted inline (e.g. 4.15.9+ for smooth streamed smoothing, 4.16.1+ for battery, 4.16.3+ for write-without-response on 0xFF01)
**Last Updated:** August 2026

This is the **wire / opcode reference** for the Narbis Edge glasses: every BLE command, its bytes, ranges, and on-device behavior. You can drive the glasses directly from raw BLE with only this document.

- Prefer a library? The **[Python SDK](../python-SDK/README.md)** and **[JS SDK](../js-SDK/README.md)** wrap every opcode here in a named method — the opcode ↔ method map is in [§ SDK method map](#sdk-method-map).
- Need the status/PPG notification frames, the OTA flow, or the earclip? Those live in the full **[protocol doc](../docs/bluetooth-protocol.md)** — this file is glasses-control only.

---

## Contents

1. [Connection](#connection)
2. [The ≥ 2-byte rule](#critical-writes-must-be--2-bytes)
3. [Legacy opacity command (1 byte)](#legacy-opacity-command-single-byte)
4. [Command table](#commands)
5. [Real-time feedback — the screen-dimmer pattern](#real-time-feedback--the-screen-dimmer-pattern)
6. [Lens config knobs (fw ≥ 4.15.7)](#lens-config-knobs-fw--4157)
7. [0xBA — breathe sync](#0xba--breathe-sync-fw--4155)
8. [Session & power](#session--power)
9. [Battery (fw ≥ 4.16.1)](#battery-fw--4161)
10. [Reconnection & disconnect behavior](#reconnection--disconnect-behavior)
11. [Standalone programs (no app)](#standalone-programs-no-app-required)
12. [Lens physics & timing](#lens-physics--timing)
13. [Defaults](#defaults)
14. [SDK method map](#sdk-method-map)
15. [Legacy — on-board coherence (unused)](#legacy--on-board-coherence-unused)
16. [Examples](#examples)

---

## Connection

| Parameter | Value |
|-----------|-------|
| Device Name | `Narbis_Edge` (exact match — filter on this; the service UUID is **not** in the advertisement) |
| Service UUID | `0x00FF` (16-bit) or `000000ff-0000-1000-8000-00805f9b34fb` (128-bit) |
| Control Characteristic | `0xFF01` (read + write) — all commands below |
| Other Characteristics | `0xFF02` OTA data, `0xFF03` status/notify, `0xFF04` PPG stream — out of scope here, see the [protocol doc](../docs/bluetooth-protocol.md) |
| MTU | 247 (requested) |
| Pairing / Bonding | None |
| Connection interval | 20–30 ms requested (the host OS BLE stack grants it; you can't set it) |
| Supervision timeout | 32 s |
| Idle Teardown | **2 minutes** with no client connected → full radio power-down; tap the magnet to re-arm advertising |
| Simultaneous Clients | **1** — advertising stops on connect; a second central cannot discover or connect while another client holds the link |
| Write Type | Write with response; **fw ≥ 4.16.3 also accepts write-without-response on `0xFF01`** (feature-detect the property) for higher-rate streaming |

**No NACKs:** the firmware never rejects a write. Out-of-range arguments are silently clamped or dropped — validate values client-side. To detect a failure, use a write timeout.

**Serialize your writes:** keep exactly one write to `0xFF01` in flight at a time. Overlapping GATT writes fail on WinRT ("operation already in progress") and Web Bluetooth alike.

---

## CRITICAL: writes must be ≥ 2 bytes

Any **1-byte write is interpreted as the legacy opacity command** (see below). Every opcode command therefore MUST be at least 2 bytes long — pad argument-less opcodes with a zero byte, e.g. `[0xA6, 0x00]`, `[0xA7, 0x00]`. A bare `[0xA6]` or `[0xA7]` is treated as an opacity write of 166 or 167, not a command.

---

## Legacy Opacity Command (Single Byte)

Any single-byte write sets lens opacity directly:

| Byte | Result |
|------|--------|
| `0x00` | Clear (0% opacity, fully transparent) |
| `0x01` - `0xFE` | Proportional opacity |
| `0xFF` | Dark (100% opacity, fully opaque) |

**Mapping:** Linear `0-255` → `0-100%` static duty.

**Behavior:** Stops any running mode (breathe/strobe/static) and holds the opacity until the next command. Not persisted. This is the cheapest continuous-feedback path — see [Real-time feedback](#real-time-feedback--the-screen-dimmer-pattern). (`0xA5` below does the same thing with an explicit opcode and a 0–100 scale.)

---

## Commands

| Opcode | Name | Arg | Persisted (NVS) | Notes |
|--------|------|-----|-----------------|-------|
| *(1 byte)* | Legacy opacity | 0-255 → 0-100% static duty | no | Stops current mode |
| `0xA0` | Lens smoothing | 0-255 (EMA τ × 10 ms; 0 = off) | yes | fw ≥ 4.15.7. On-device glide between commanded static targets — see [Lens config](#lens-config-knobs-fw--4157) |
| `0xA1` | Lens max transition rate | 0-100 %/100 ms (0 = unlimited) | yes | fw ≥ 4.15.7. Hard slew cap on commanded static, applied after `0xA0` — see [Lens config](#lens-config-knobs-fw--4157) |
| `0xA2` | Brightness | 0-100 % | yes | The persistent **max-tint / breathe depth** that scales breathe & strobe output. On **fw ≥ 4.16.2** it is owned **solely** by `0xA2` (a `0xA5` static write no longer disturbs it); on fw ≤ 4.16.1 `0xA5` shared the same variable, so a streamed `0xA5` overwrote brightness. Immediate; does not change mode |
| `0xA3` | On-disconnect behavior | `0x00` continue (default) / `0x01` fail clear | yes | fw ≥ 4.15.7 — see [Reconnection](#reconnection--disconnect-behavior) |
| `0xA4` | Session duration | 1-60 min | yes | Device auto-sleeps at session end — see [Session & power](#session--power) |
| `0xA5` | Static mode + duty | 0-100 % | no | Enters static mode at the given duty. The main real-time-feedback command. On **fw ≥ 4.16.2** it's a clean static-duty write that does **not** touch `0xA2` brightness or other programs (see `0xA2`) |
| `0xA6` | Start strobe mode | ignored (send `0x00`) | no | Uses stored frequency/duty (`0xAB`/`0xAC`); needs an active session |
| `0xA7` | Sleep now | ignored (send `0x00`) | no | Immediate deep sleep |
| `0xAB` | Strobe frequency | 1-50 Hz | yes | 3-byte form `[0xAB, lo, hi]` sets 0.1 Hz precision (u16 LE deci-Hz), fw ≥ 4.14.41 |
| `0xAC` | Strobe duty | 10-90 % | yes | Dark-phase duty |
| `0xB0` | Start breathe mode | `0x00` breathe / `0x01` breathe+strobe | no | Uses stored breathe parameters; `0x01` requires fw ≥ 4.15.6. A **bare `[0xB0]` is not breathe** — it's a 1-byte opacity write (~69%); always send the 2-byte form |
| `0xB1` | Breathe rate | 1-30 BPM (integer) | yes | Boot default 6 BPM |
| `0xB2` | Breathe inhale ratio | 10-90 % | yes | Portion of the cycle spent inhaling |
| `0xB3` | Breathe hold-top | 0-50 (× 100 ms) | yes | Hold at full dark |
| `0xB4` | Breathe hold-bottom | 0-50 (× 100 ms) | yes | Hold at clear |
| `0xB5` | Breathe waveform | 0 sine / 1 linear | yes | |
| `0xBA` | Breathe sync | `[cycle_ms:u16 LE][inhale_pct:u8]` | no | Fractional-rate phase lock — see [§0xBA](#0xba--breathe-sync-fw--4155) |
| `0xBF` | Factory reset | ignored (send `0x00`) | — | Resets all persisted settings to defaults |
| `0xC7` | Battery poll / probe diag | `0x00` refresh / `0x01` reprobe ADC | no | fw ≥ 4.16.1. Refreshes the battery reading (or re-runs the ADC channel probe with `0x01`); result lands on `0x180F` / the `0xFB` status frame — see [Battery](#battery-fw--4161) |
| `0xA8`/`0xA9`/`0xAA`/`0xAD` | OTA | — | — | Firmware update flow; see the [protocol doc](../docs/bluetooth-protocol.md) |

> **Argument clamping is not universal.** Most opcodes clamp out-of-range args into range, but a few **ignore** the write instead: `0xB7`/`0xB8` (legacy coherence) drop out-of-range args, and `0xE0` rejects the whole write on a validation failure. None of them NACK.

---

## Real-time feedback — the screen-dimmer pattern

The core integration: map any protocol's feedback value to lens tint — a wearable screen dimmer. Dim when the trainee is out of condition, clear when in condition.

**Proportional feedback** (a dimmer that tracks a continuous signal):

1. `[0xA4, minutes]` once — a session guard so auto-sleep doesn't end the session early ([Session & power](#session--power)).
2. On each feedback update, map your value → duty 0-100 and write `[0xA5, duty]`.
3. **Rate:** the firmware applies each `[0xA5, duty]` on its 10 ms tick (100 Hz internally, no throttling) — the **BLE link** is the only limit. The glasses request a 20–30 ms interval with slave latency 1 (~33–50 connection events/sec): a host on the default parameters sustains **~8–11 writes/sec**, and one that requests throughput-optimized connection parameters (e.g. WinRT `RequestPreferredConnectionParameters(ThroughputOptimized)`) reaches **~20 writes/sec** (both measured on fw 4.16.2). The full **30–50 Hz** band uses firmware write-without-response on `0xFF01`, available on **fw ≥ 4.16.3** (older firmware is write-with-response only). **Recommended: target 30–50 Hz with coalescing on** — the effective rate self-limits to your data rate. There is no 12 Hz or 20 Hz limit: the old "12 Hz" was the breathe pacer, and the "20 Hz ceiling" was a stale conservative figure. Decimate a faster signal — you don't need a write per sample.
4. **Coalesce:** skip the write if `duty` is unchanged — the lens holds its state.
5. **One write in flight:** never overlap writes (see [Connection](#connection)) — write-with-response paces throughput, so the configured rate is a *target*, never a queue.

> **fw ≥ 4.16.2 — clean static:** `[0xA5, duty]` (and the 1-byte opacity write) now go through `lens_apply_static(duty)` and no longer write the `0xA2` brightness variable. On fw ≤ 4.16.1 they shared it, so streaming a dimmer down to `0` left `brightness = 0` and made every *other* program (breathe/strobe/coherence) render clear afterward. On 4.16.2 a streamed `0xA5` no longer disturbs `0xA2` or other programs — ideal for real-time dimming — and boot self-heals a persisted brightness of 0. See [`0xA2`](#commands).

**Discrete reward** (reinforce the instant a contingency is met — operant conditioning): don't wait for your streaming loop's next tick — send `[0xA5, duty]` (typically `[0xA5, 0]` = clear) immediately. `0xA5` applies with no smoothing by default, so reward latency is just ~20–60 ms BLE transport + a < 100 ms lens switch, bounded by your upstream analysis window, not the streaming rate. (If you have enabled `0xA0`/`0xA1`, those deliberately stretch the transition — leave them off for minimum-latency rewards.)

> Both patterns are built into the SDKs' `FeedbackStream` (`feed()`/`feed_reward()` for the stream, `reward_event()` for immediate discrete rewards). See the [Python](../python-SDK/README.md) / [JS](../js-SDK/README.md) READMEs.

---

## Lens config knobs (fw ≥ 4.15.7)

Three persisted knobs shape how commanded **static** transitions render. All default `0` = pre-4.15.7 behavior (snap, no cap, freeze-on-disconnect), so they're safe to send unconditionally — older firmware ignores them. They affect **only commanded static duty** (`0xA5`, the 1-byte opacity write, and the `0xA3` fail-clear); strobe, breathe, the standalone programs, and the coherence-lens are untouched.

> **Write confirmation (fw ≥ 4.15.11):** each `0xA0`/`0xA1`/`0xA3` write emits a `0xF1` text log frame on `0xFF03` describing what landed (e.g. `"Lens smoothing: tau=80ms (level 8)"`, `"Lens max rate: 5%/100ms"`, `"On-disconnect: fail clear"`). There is still **no config readback**, but a client subscribed to `0xFF03` can watch the `0xF1` frame to confirm a write took effect.

### `0xA0` — Lens smoothing (EMA)

`[0xA0, τ]`, τ in ×10 ms units (0-255 → 0-2.55 s), 0 = off. The firmware EMA-glides between commanded static targets instead of snapping — **this is the recommended way to get smooth real-time feedback without over-sending.**

When you stream a live feedback value at ~20–30 Hz, the raw stream is *stepped* (each write is a discrete jump) and a noisy signal makes the lens visibly jitter. Set `0xA0` and the lens **glides between your writes**, bridging the gaps into continuous motion with no visible stepping, and it absorbs per-sample noise on-device so you don't have to filter client-side. RF-retransmit gaps and rate drops render as smooth motion too. Send it once at connect (persisted). Optionally pair it with `0xA1` as a hard safety cap.

**Tuning:** τ ≈ 1–2× your write period. A 30 Hz stream (~33 ms) → τ arg **3–6**; **arg 8 (≈ 80 ms)** is a good general-purpose value.

> **Streaming needs fw ≥ 4.15.9.** On 4.15.7 the EMA accumulator is re-seeded on every write, so a continuous stream stalls ~2–4 % short of each target (fixed in 4.15.8). Through 4.15.8 the smoothed output was still floored to the 101-level integer-duty grid, so the lens showed ~1 %-duty stepping even at max τ; **4.15.9** drives the lens at full 10-bit PWM resolution. One-shot writes are fine on any of them.

### `0xA1` — Lens max transition rate (slew cap)

`[0xA1, rate]`, rate in %/100 ms (0-100, 0 = unlimited). A hard slew limit on commanded static transitions, applied **after** the `0xA0` glide — a safety envelope guaranteeing the lens can't snap even if a host streams garbage. `40` ≈ full-scale in 250 ms (the breathe engine's own internal limit). Breathe/strobe waveforms are unaffected.

### `0xA3` — On-disconnect behavior

Covered in [Reconnection](#reconnection--disconnect-behavior).

### Ramping tint over a fixed time (Tint X → Y over Z s)

To fade the lens from one opacity to another over a set duration, use either the on-device slew ramp (one write) or client-side interpolation (full curve control).

**1. Firmware slew ramp — one write, on-device (recommended for a fixed fade, fw ≥ 4.15.7).** Set the slew cap `0xA1` so the firmware ramps at a constant rate, then write the target once with `0xA5`. To move `Δduty` percent over `Z` seconds:

```
slew (%/100ms) = round(Δduty / (Z × 10))     # clamp 1–100; 0 = unlimited/instant
```

Example — fade fully clear → dark (Δ = 100) over 2 s: `slew = 100 / (2 × 10) = 5` →

```
Write: [0xA1, 5]      # slew cap 5 %/100ms
Write: [0xA5, 100]    # target; lens ramps over ~2 s on-device
Write: [0xA1, 0]      # afterward: later writes snap again
```

The slew is a *fixed rate*, so if you want an exact duration regardless of the starting point, recompute `slew` from the actual `Δ` each time.

**2. Client-side interpolation (full curve control, any firmware).** Step `0xA5` from X to Y yourself at your write rate — this lets you do linear, ease-in/out, or any curve:

```
steps  = Z × rate
duty_i = round(X + (Y − X) · i / steps)       # i = 1 … steps
# write each duty_i at 1/rate spacing (coalesced — skip unchanged values)
```

---

## 0xBA — Breathe Sync (fw ≥ 4.15.5)

Phase-locks the breathe engine to an external pacer, and is the only way to get **fractional** breathing rates (`0xB1` is integer-BPM). 4 bytes on the wire:

| Byte | Value |
|------|-------|
| 0 | `0xBA` |
| 1-2 | `cycle_ms` (u16, little-endian) — exact breath cycle length in ms; valid 2000-30000, silently clamped |
| 3 | `inhale_pct` (u8) — inhale ratio 10-90 %, silently clamped |

**Behavior:** restarts the breathe cosine at the instant of the write and sets the exact cycle length in milliseconds.

**Boundary-only rule:** send `0xBA` only at the breath-cycle boundary, never mid-breath — the waveform restarts on write, so a mid-breath sync causes a visible jump.

**Per-breath keep-alive:** the sync auto-expires **2 cycles** after the last `0xBA` write, reverting to the stored integer `0xB1` BPM. To hold a fractional rate you **must** re-send `0xBA` every breath at the boundary — sending it once silently reverts within two breaths. (The expiry is time-based, so it applies while connected too.)

Older firmware ignores `0xBA` — safe to send unconditionally.

---

## Session & power

The glasses run a **session timer**. It starts at device wake/boot (or sensor plug-in), runs for the session duration, and at expiry the device **enters deep sleep** — the lens goes dark and BLE drops.

- Default duration is **30 minutes**; `0xA4` sets 1–60 min and is **persisted**, so a previous client's value survives across connects.
- Writing `0xA4` sets the total but does **not** restart the clock. At session start, write `[0xA4, minutes]` ≥ your planned session.
- Sessions longer than 60 min require a mid-session re-wake (magnet tap).
- `0xA7` sleeps immediately; a magnet close ≥ 5 s also sleeps.

> **Battery (fw ≥ 4.16.1, V1.2+ hardware):** the glasses now expose battery over BLE — via the standard **Battery Service `0x180F`** (read + notify `0x2A19`, 0-100, which **reads `0` while the level is unknown**), the **`0xFB` status frame** on `0xFF03`, and the **`0xC7` poll**. See the new [Battery](#battery-fw--4161) section for the frame layout and the "unknown vs 0 %" nuance. Boards without the sense divider (and firmware ≤ 4.16.0) report unsupported. There is still no session-remaining readout, and the Edge exposes **no DIS**, so **firmware version is not available via a plain GATT read** — but it *is* pushed as a `0xF1` text status frame on `0xFF03` (`"Narbis fw v…"`) whenever a client subscribes, and the `0xF3` health frame carries `uptime_s` (shares the session clock's origin). See the protocol doc.

---

## Battery (fw ≥ 4.16.1)

On **fw ≥ 4.16.1** with **V1.2+ hardware**, the glasses report battery state over BLE through three surfaces. The sense pin is **GPIO36 / SENSOR_VP / ADC1_CH0** (V1.2 respin); boards without the sense divider report unsupported.

| Surface | Where | Payload | Notes |
|---------|-------|---------|-------|
| **Battery Service** | `0x180F` → `0x2A19` (read + notify) | `u8` percent (0-100) | The clean, SDK-friendly path. **Reads `0` while the level is unknown** — it can't distinguish "unknown" from a real 0 %. Absent entirely on fw ≤ 4.16.0 |
| **`0xFB` status frame** | notify on `0xFF03` | `[mv:u16 LE][soc:u8][charging:u8]` | Same layout as the earclip relay `0xF8`. `soc = 0xFF` → unknown/unsupported. The **authoritative** source that distinguishes unknown / charging / millivolts |
| **`0xC7` poll** | write on `0xFF01` | `[0xC7, 0x00]` refresh / `[0xC7, 0x01]` reprobe ADC | Refreshes the reading (or re-runs the ADC channel probe); result lands on `0x180F` + the `0xFB` frame |

**Unsupported units:** a board without the divider (or any fw ≤ 4.16.0) reports `mv = 0`, `soc = 0xFF`, `charging = 0xFF`, and does not register `0x180F`. Treat a **`soc` of `0xFF`** or a **missing `0x180F` service** as "battery not available on this unit."

**SDK note (`get_battery()` / `getBattery()`):** the SDK reads `0x2A19` and returns the 0-100 integer, or `None`/`null` when `0x180F` is absent (pre-4.16.1 firmware). Because `0x2A19` reads `0` while the level is unknown, a returned `0` may mean *unknown* on some builds — for a true millivolt / charging / unknown flag, read the `0xFB` frame on `0xFF03` (see the protocol doc).

---

## Reconnection & disconnect behavior

- **CCCD subscriptions are lost** on disconnect — re-subscribe on every reconnect.
- **Lens state is NOT lost.** By default the lens **freezes** at its last commanded output across a disconnect: a crashed app leaves the last tint in place (e.g. fully dark) until reconnect, a magnet action, or session-expiry sleep. So before an *intentional* disconnect, send `[0xA5, 0x00]` (clear) or `[0xA7, 0x00]` (sleep) so the wearer isn't left dark.
- **Fail-clear (fw ≥ 4.15.7):** write `[0xA3, 0x01]` once (persisted) and the glasses instead stop any strobe and drop to a clear static lens on any disconnect (riding the `0xA0` glide if set). The failsafe fires when the firmware declares the link dead, bounded by the **32 s** supervision timeout — a crashed app can still leave the wearer dark for up to ~32 s, so the pre-disconnect clear write remains good practice.
- **Re-assert on reconnect.** After reconnecting, re-subscribe to CCCDs and, if you were pacing a fractional breathe rate, resume sending `0xBA` (its 2-cycle time-based sync will have lapsed). Lens mode and static duty are held in RAM and, by default (`0xA3` = 0), **persist** across a plain disconnect (see above) — but they are lost on sleep/reboot, so re-asserting your full lens setup on reconnect is still good practice. **NVS-persisted** opcodes survive both disconnects and reboots: `0xA2`, `0xA4`, `0xAB`, `0xAC`, `0xB1`–`0xB5` (plus fw ≥ 4.15.7 `0xA0`, `0xA1`, `0xA3`).
- No application keep-alive is needed while connected — the 2-minute teardown applies only when *no* client is connected.

---

## Standalone Programs (no app required)

A short magnet tap (0.15-4 s) on the temple cycles the on-board programs. The lens signals a program change with N slow fade-dark pulses. These render from the same NVS-persisted parameters the opcodes write (breathe rate/shape `0xB1`/`0xB2`/`0xB5`, strobe `0xAB`/`0xAC`), so values your app persists change them.

| Program | Behavior (factory defaults) |
|---------|----------|
| 1 — BREATHE | 6 BPM sine, lens tint follows the waveform (boot default) |
| 2 — BREATHE+STROBE | 10 Hz strobe whose dark-phase duty is modulated by the breathing waveform |
| 3 — STROBE | Plain 10 Hz strobe |

A long magnet close (≥ 5 s) enters deep sleep. **Magnet gestures stay live while an app is connected** (only OTA suspends them) — a mid-session tap overwrites the app's lens mode, so watch for it if it matters to your protocol.

---

## Lens physics & timing

- **Opacity floor:** duty 1-100 % maps to raw PWM 265-1023 (fw ≥ 4.15.4) — a perceptual floor that skips the invisible low range. Duty 0 is fully clear.
- **Switching time (electrochromic cell):** Ton (transparent→dark) 2.5-40 ms, Toff (dark→transparent) 2.5-50 ms; < 100 ms all modes, slower when cold. Fast enough that the lens is not the latency bottleneck for feedback.
- `0xA5` applies on-device immediately by default (the breathe slew limiter is breathe-mode only) — unless you've set the `0xA0`/`0xA1` knobs.

---

## Defaults

| Parameter | Default |
|-----------|---------|
| Session duration | 30 min (persisted) |
| Breathe rate | 6 BPM (boot default, program 1) |
| Standalone strobe | 10 Hz |
| Lens smoothing (`0xA0`) | 0 (off / snap) |
| Lens slew cap (`0xA1`) | 0 (unlimited) |
| On-disconnect (`0xA3`) | 0 (freeze / continue) |

Parameters marked "persisted" survive sleep and power cycles (NVS); `0xBF` restores factory values.

---

## SDK method map

| Opcode | Python (`edge_glasses`) | JavaScript (`edge-glasses`) |
|--------|--------------------------|------------------------------|
| *(1 byte)* opacity | `set_opacity(0-255)` / `clear()` / `dark()` | `setOpacity` / `clear` / `dark` |
| `0xA0` | `set_lens_smoothing(ms)` | `setLensSmoothing(ms)` |
| `0xA1` | `set_lens_max_rate(pct_per_100ms)` | `setLensMaxRate(pctPer100ms)` |
| `0xA2` | `set_brightness(0-100)` | `setBrightness` |
| `0xA3` | `set_disconnect_behavior(fail_clear)` | `setDisconnectBehavior(failClear)` |
| `0xA4` | `set_duration(1-60)` | `setDuration` |
| `0xA5` | `set_static(0-100)` | `setStatic` |
| `0xA6` | `start_strobe(hz?, duty_pct?)` | `startStrobe` |
| `0xA7` | `sleep()` | `sleep` |
| `0xAB`/`0xAC` | `set_strobe_frequency` / `set_strobe_duty` | `setStrobeFrequency` / `setStrobeDuty` |
| `0xB0`–`0xB5` | `start_breathe(...)` | `startBreathe({...})` |
| `0xBA` | `sync_breath(cycle_ms, inhale_pct)` | `syncBreath(cycleMs, inhalePct)` |
| `0xBF` | `factory_reset()` | `factoryReset()` |
| `0xC7` / `0x2A19` | `get_battery()` → 0-100 or `None` | `getBattery()` → 0-100 or `null` |
| (WNR fast path) | `supports_fast_write` / `_stream_static()` | `supportsFastWrite` / `streamStatic()` |
| real-time stream | `start_feedback_stream()` → `FeedbackStream` | `startFeedbackStream()` → `FeedbackStream` |
| real-time stream | `start_feedback_stream()` → `FeedbackStream` | `startFeedbackStream()` → `FeedbackStream` |

---

## Legacy — On-board Coherence (unused)

The firmware retains an on-board coherence/biofeedback pipeline: `0xB6` pulse-on-beat, `0xB7` PPG program 0-3, `0xB8` coherence difficulty, `0xB9` adaptive pacer, `0xCA` external-IBI injection, `0xCB` HR source, `0xD0` detector reset, `0xE0` coherence tuning. These are functional but no longer used — all processing is app-side now. The Edge↔earclip BLE relay is compile-disabled on stock builds. Full details: [protocol doc §4.8](../docs/bluetooth-protocol.md#48-legacy-on-board-coherence-pipeline-unused).

---

## Examples

**Configure a custom strobe, then start it:**
```
Write: [0xA2, 0x64]              # brightness 100%
Write: [0xAB, 0x0A]              # strobe 10 Hz
Write: [0xAC, 0x32]              # strobe duty 50%
Write: [0xA4, 0x0A]             # 10-min session
Write: [0xA6, 0x00]              # start strobe
```

**Real-time feedback (screen dimmer), raw wire:**
```
Write: [0xA4, 0x3C]             # 60-min session guard
loop at your feedback rate (target 30-50 Hz, coalesced):
  Write: [0xA5, duty]           # duty 0 (clear) .. 100 (dark); skip if unchanged
```

**Paced breathing with a fractional rate:**
```
Write: [0xB1, 0x06]             # 6 BPM (integer fallback)
Write: [0xB2, 0x28]             # 40% inhale ratio
Write: [0xB5, 0x00]             # sine waveform
Write: [0xB0, 0x00]             # start breathe mode
# then, once per breath at the cycle boundary (required to hold the rate):
Write: [0xBA, 0x10, 0x27, 0x28]  # 0x2710 = 10000 ms cycle, 0x28 = 40% inhale
```

**Smooth a low-rate stream + fail-clear on disconnect (fw ≥ 4.15.7):**
```
Write: [0xA0, 0x0A]             # 100 ms smoothing glide
Write: [0xA3, 0x01]             # clear the lens if the app link drops
```
