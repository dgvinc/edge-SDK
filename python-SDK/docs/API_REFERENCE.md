# EDGE Glasses BLE API Reference

**Firmware Version:** 4.0  
**Last Updated:** February 2026

---

## Connection

| Parameter | Value |
|-----------|-------|
| Device Name | `Smart_Glasses` |
| Service UUID | `0x00FF` (16-bit) or `000000ff-0000-1000-8000-00805f9b34fb` (128-bit) |
| Characteristic UUID | `0xFF01` (16-bit) or `0000ff01-0000-1000-8000-00805f9b34fb` (128-bit) |
| Write Type | Write with response |

---

## Commands

### Legacy Command (Single Byte)

Any single byte write is treated as a direct opacity command:

| Byte | Result |
|------|--------|
| `0x00` | Clear (0% opacity, fully transparent) |
| `0x01` - `0xFE` | Proportional opacity |
| `0xFF` | Dark (100% opacity, fully opaque) |

**Mapping:** Linear `0-255` → `0-100%` duty cycle

**Behavior:** Stops any running session, holds static opacity until next command.

**Example:**
```
Write: [0x80]  → 50% opacity
Write: [0xFF]  → 100% opacity (full dark)
Write: [0x00]  → 0% opacity (clear)
```

---

### Extended Commands (Multi-byte)

All extended commands start with `0xA_` prefix to avoid collision with legacy single-byte range.

#### 0xA1 - Set Strobe Range

Set the strobe frequency progression for timed sessions.

| Byte | Value |
|------|-------|
| 0 | `0xA1` |
| 1 | `start_hz` (1-50) |
| 2 | `end_hz` (1-50) |

**Behavior:** Restarts session. Frequency progresses linearly from start to end over session duration.

**Example:**
```
Write: [0xA1, 0x0C, 0x08]  → 12Hz to 8Hz
Write: [0xA1, 0x0F, 0x04]  → 15Hz to 4Hz
```

---

#### 0xA2 - Set Brightness

Set maximum brightness level.

| Byte | Value |
|------|-------|
| 0 | `0xA2` |
| 1 | `brightness` (0-100) |

**Behavior:** Does NOT restart session. Takes effect immediately.

**Example:**
```
Write: [0xA2, 0x64]  → 100% brightness
Write: [0xA2, 0x50]  → 80% brightness
```

---

#### 0xA3 - Set Breathing Pattern

Configure the 4-phase breathing cycle.

| Byte | Value |
|------|-------|
| 0 | `0xA3` |
| 1 | `inhale_time` (×0.1 seconds) |
| 2 | `hold_in_end` (×0.1 seconds) |
| 3 | `exhale_time` (×0.1 seconds) |
| 4 | `hold_out_end` (×0.1 seconds) |

**Phases:**
1. **Inhale:** Lenses go clear → dark (fixed duration)
2. **Hold In:** Lenses stay dark (grows from 0 to `hold_in_end`)
3. **Exhale:** Lenses go dark → clear (fixed duration)
4. **Hold Out:** Lenses stay clear (grows from 0 to `hold_out_end`)

**Behavior:** Restarts session. Hold times progress linearly over session duration.

**Example:**
```
Write: [0xA3, 0x28, 0x28, 0x28, 0x28]  → 4.0s inhale, 0→4.0s hold, 4.0s exhale, 0→4.0s hold
Write: [0xA3, 0x32, 0x32, 0x32, 0x32]  → 5.0s all phases
```

---

#### 0xA4 - Set Session Duration

Set the total session length.

| Byte | Value |
|------|-------|
| 0 | `0xA4` |
| 1 | `minutes` (1-60) |

**Behavior:** Restarts session. Device auto-sleeps when session ends.

**Example:**
```
Write: [0xA4, 0x0A]  → 10 minutes
Write: [0xA4, 0x14]  → 20 minutes
```

---

#### 0xA5 - Static Override

Hold at a fixed duty cycle, stopping any running session.

| Byte | Value |
|------|-------|
| 0 | `0xA5` |
| 1 | `duty` (0-100) |

**Behavior:** Stops session, holds static duty until `0xA6` resume or new command.

**Example:**
```
Write: [0xA5, 0x32]  → Hold at 50%
Write: [0xA5, 0x64]  → Hold at 100%
Write: [0xA5, 0x00]  → Hold at 0% (clear)
```

---

#### 0xA6 - Resume Session

Restart the timed session with current parameters.

| Byte | Value |
|------|-------|
| 0 | `0xA6` |

**Behavior:** Clears override, restarts session from beginning.

**Example:**
```
Write: [0xA6]  → Restart session
```

---

#### 0xA7 - Sleep

Enter deep sleep immediately.

| Byte | Value |
|------|-------|
| 0 | `0xA7` |

**Behavior:** PWM off, enters deep sleep. Wake by opening arms (Hall sensor).

**Example:**
```
Write: [0xA7]  → Sleep now
```

---

## Command Summary Table

| Command | Bytes | Description | Restarts Session |
|---------|-------|-------------|------------------|
| Legacy | `[0x00-0xFF]` | Direct opacity 0-255 | Stops session |
| Strobe | `[0xA1, start, end]` | Set Hz range 1-50 | Yes |
| Brightness | `[0xA2, pct]` | Set max brightness 0-100 | No |
| Breathing | `[0xA3, inh, h_in, exh, h_out]` | Set pattern (×0.1s) | Yes |
| Duration | `[0xA4, mins]` | Set length 1-60 min | Yes |
| Override | `[0xA5, duty]` | Hold at duty 0-100% | Stops session |
| Resume | `[0xA6]` | Restart session | Yes |
| Sleep | `[0xA7]` | Enter deep sleep | N/A |

---

## Default Values

| Parameter | Default |
|-----------|---------|
| Session Duration | 10 minutes |
| Strobe Start | 12 Hz |
| Strobe End | 8 Hz |
| Brightness | 100% |
| Inhale Time | 4.0s |
| Hold In End | 4.0s |
| Exhale Time | 4.0s |
| Hold Out End | 4.0s |

---

## Session Behavior

1. **Boot:** Device wakes, starts session automatically
2. **Running:** Strobe frequency and hold times progress linearly
3. **End:** Session completes, device enters deep sleep
4. **Wake:** Open arms to wake and start new session

---

## Hardware Notes

| Item | Value |
|------|-------|
| MCU | ESP32-PICO-D4 |
| PWM Pin | GPIO27 |
| Hall Sensor | GPIO4 (LOW = arms open) |
| PWM Frequency | 1 kHz |
| PWM Dead Zone | Duty 1-100% maps to raw 400-1024 (skips invisible range) |
| Active Current | ~29 mA |
| Sleep Current | ~16 µA |

---

## Example: Configure Custom Session

To start a 20-minute relaxation session (10→4 Hz, 5s breathing):

```
Write: [0xA2, 0x64]                    # 100% brightness
Write: [0xA3, 0x32, 0x32, 0x32, 0x32]  # 5.0s breathing pattern
Write: [0xA1, 0x0A, 0x04]              # 10→4 Hz strobe
Write: [0xA4, 0x14]                    # 20 minutes (restarts session)
```

Order matters: send duration last since it restarts the session.
