# EDGE Glasses — C / C++ SDK

Control Narbis EDGE smart glasses from C or C++. Same API surface as the
[Python](../python-SDK/) and [JavaScript](../js-SDK/) SDKs, and byte-for-byte
identical on the wire — enforced by a
[cross-language golden-vector test](#cross-language-verification).

**Requires C++17** for the library. The C API works from **C89 onward**.

---

## Which piece do you need?

The SDK is deliberately layered, so you can take only the part you want.

| You have… | Use | What you get |
|---|---|---|
| Your own Bluetooth stack, and you want C++ | [`edge/protocol.hpp`](include/edge/protocol.hpp) | Header-only, zero dependencies. Builds the exact frame bytes with all clamping and padding applied. |
| Your own Bluetooth stack, and you want C | [`edge_frame_*`](include/edge/edge_glasses.h) | The same builders behind a flat C ABI. |
| Your own Bluetooth stack, and you want the streaming logic too | `edge::CallbackTransport` / `edge_glasses_create_with_callbacks` | Adds command sequencing, write serialization and the real-time feedback stream on top of two callbacks. |
| Nothing yet, on Windows | `edge::WinRtTransport` / `edge_glasses_create_winrt` | The whole thing, including the BLE connection. |

Nothing above the first row is mandatory. A C application that already speaks
GATT can use this SDK as a pure protocol encoder and never link the rest.

---

## Quick start — C++

```cpp
#include "edge/glasses.hpp"
#include "edge/winrt_transport.hpp"

edge::WinRtTransport transport;
edge::Glasses glasses(&transport);

glasses.connect();            // scans for "Narbis_Edge"
glasses.set_duration(60);     // session guard: no auto-sleep for 60 min
glasses.set_static(50);       // 0 = clear .. 100 = fully dark
glasses.clear();              // never leave the wearer dark
glasses.disconnect();
```

## Quick start — C

```c
#include "edge/edge_glasses.h"

edge_glasses g;
if (edge_glasses_create_winrt(&g) != EDGE_OK) { /* handle */ }
if (edge_glasses_connect(g, 10000) != EDGE_OK) {
    fprintf(stderr, "%s\n", edge_last_error());   /* e.g. tap the magnet */
}
edge_glasses_set_duration(g, 60);
edge_glasses_set_static(g, 50);
edge_glasses_clear(g);
edge_glasses_destroy(g);
```

## Quick start — protocol only (no SDK objects at all)

```c
uint8_t frame[EDGE_MAX_FRAME];
int n = edge_frame_set_static(frame, sizeof frame, 50);
my_gatt_write(EDGE_UUID_CONTROL, frame, (size_t)n);   /* -> A5 32 */
```

```cpp
auto f = edge::protocol::set_static(50);
my_gatt_write(edge::uuid::kControl, f.data(), f.size());
```

---

## The core integration: a wearable screen dimmer

Classic neurofeedback dims the training display when the trainee falls out of
condition and clears it when they are in condition. The Edge does the same on
the lens itself, so it drops into **any protocol** (SMR, alpha/theta, HEG, EMG
down-training, HRV…) wherever your software already produces a feedback value.

`FeedbackStream` ships that whole contract — decimation, coalescing, write
serialization — so you never hand-roll a BLE cadence loop:

```cpp
auto stream = glasses.start_feedback_stream(30.0);   // ~30 Hz writer

// From your pipeline, any thread, any rate:
stream->feed_reward(value);       // 0..1, where 1 = in condition = clear
// or with your dimmer's existing 0-100 % scale:
stream->feed(duty);

// The instant a contingency is met — bypasses the tick entirely:
stream->reward_event(/*duty=*/0, /*hold_ms=*/150);

stream->stop();                   // stops the writer and clears the lens
```

In C:

```c
edge_stream s;
edge_glasses_start_feedback_stream(g, 30.0, &s);
edge_stream_feed_reward(s, value);        /* 0..1, any thread, any rate */
edge_stream_reward_event(s, 0, 150);      /* discrete reward, delivered now */
edge_stream_stop(s, 1);                   /* stop and clear */
```

### Proportional vs. discrete reward

These have different latency needs, and only the first is governed by the
streaming cadence.

- **Proportional** (`feed` / `feed_reward`) — the tint tracks a continuously
  varying signal. A 30 Hz stream gives ~33 ms granularity, far below the
  upstream EEG analysis window (typically 250 ms – 1 s) that dominates the loop.
- **Discrete** (`reward_event`) — reinforcement the instant a contingency is
  met. This writes immediately instead of waiting up to one stream period, so
  latency is ~20–60 ms transport + ~40–50 ms lens switch. It preempts the
  proportional stream, waiting at most one in-flight write.

### Smooth motion off a noisy signal

Send `set_lens_smoothing(80)` **once at connect**. The firmware then glides
between your commanded targets with an ~80 ms EMA, so the lens moves
continuously between writes instead of stepping, and per-sample noise is
absorbed on-device rather than in your filter chain. It is persisted, and older
firmware ignores it, so it is always safe to send.

Pair it with `set_lens_max_rate(40)` as a hard safety envelope: the lens then
cannot snap even if a host streams garbage. Leave both off when you want
minimum-latency discrete rewards.

---

## Building

```bash
cmake -S cpp-SDK -B cpp-SDK/build
cmake --build cpp-SDK/build --config Release
ctest --test-dir cpp-SDK/build --output-on-failure
```

| CMake option | Default | Effect |
|---|---|---|
| `EDGE_WITH_WINRT` | `ON` under MSVC, else `OFF` | Build the Windows BLE transport. Requires MSVC and Windows SDK 10.0.19041+; C++/WinRT is not available under MinGW or GCC. Verified against VS 2022 Build Tools with SDK 10.0.22621. |
| `EDGE_BUILD_TESTS` | `ON` | Build the three test binaries. |
| `EDGE_BUILD_EXAMPLES` | `ON` | Build the examples. |
| `EDGE_BUILD_SHARED` | `OFF` | Build a DLL instead of a static library. |

Consuming it from your own CMake project:

```cmake
add_subdirectory(cpp-SDK)
target_link_libraries(my_app PRIVATE edge::glasses)
```

There is nothing to link if you only want `protocol.hpp` — add
`cpp-SDK/include` to your include path and you are done.

### Without CMake

The library is four translation units and has no dependencies beyond the
standard library and a threading library:

```bash
g++ -std=c++17 -pthread -Icpp-SDK/include -c \
    cpp-SDK/src/glasses.cpp cpp-SDK/src/feedback_stream.cpp cpp-SDK/src/c_api.cpp
```

Add `cpp-SDK/src/winrt_transport.cpp` and `-DEDGE_WITH_WINRT` under MSVC.

---

## API reference

### Connection

| C++ | C | Notes |
|---|---|---|
| `Glasses(ITransport*)` | `edge_glasses_create_with_callbacks` / `_winrt` | |
| `connect(timeout_ms)` | `edge_glasses_connect` | Scans for `Narbis_Edge`. Throws `DeviceNotFoundError` / returns `EDGE_ERR_DEVICE_NOT_FOUND` when nothing is advertising. |
| `disconnect()` | `edge_glasses_disconnect` | The lens **freezes** at its last tint unless fail-clear is enabled. |
| `is_connected()` | `edge_glasses_is_connected` | |
| `supports_fast_write()` | `edge_glasses_supports_fast_write` | True on firmware ≥ 4.16.3. |
| `WinRtTransport::scan(ms)` | — | Static; returns devices strongest-signal first. |

### Lens control

| C++ | C | Wire | Range |
|---|---|---|---|
| `set_opacity(v)` | `edge_glasses_set_opacity` | `[v]` — **1 byte** | 0–255 |
| `clear()` / `dark()` | `edge_glasses_clear` / `_dark` | `[0x00]` / `[0xFF]` | |
| `set_static(duty)` | `edge_glasses_set_static` | `[0xA5, duty]` | 0–100 % |
| `set_brightness(pct)` | `edge_glasses_set_brightness` | `[0xA2, pct]` | 0–100 %, persisted |
| `set_duration(min)` | `edge_glasses_set_duration` | `[0xA4, min]` | 1–60 min, persisted |
| `set_lens_smoothing(ms)` | `edge_glasses_set_lens_smoothing` | `[0xA0, ms/10]` | 0–2550 ms, persisted, fw ≥ 4.15.7 |
| `set_lens_max_rate(r)` | `edge_glasses_set_lens_max_rate` | `[0xA1, r]` | 0–100 %/100 ms, persisted, fw ≥ 4.15.7 |
| `set_disconnect_behavior(b)` | `edge_glasses_set_disconnect_behavior` | `[0xA3, 0\|1]` | persisted, fw ≥ 4.15.7 |

### Modes

| C++ | C | Wire |
|---|---|---|
| `start_strobe(hz, duty)` | `edge_glasses_start_strobe` | `[0xAB…]`, `[0xAC…]`, `[0xA6, 0x00]` |
| `set_strobe_frequency(hz)` | `edge_glasses_set_strobe_frequency` | `[0xAB, hz]`, 1–50 Hz |
| `set_strobe_duty(pct)` | `edge_glasses_set_strobe_duty` | `[0xAC, pct]`, 10–90 % |
| `start_breathe(options)` | `edge_glasses_start_breathe` | `[0xB1…0xB5]` then `[0xB0, 0\|1]` |
| `sync_breath(cycle_ms, inhale)` | `edge_glasses_sync_breath` | `[0xBA, lo, hi, inhale]`, fw ≥ 4.15.5 |

### Power, presets, low level

| C++ | C |
|---|---|
| `sleep()` / `factory_reset()` | `edge_glasses_sleep` / `_factory_reset` |
| `get_battery()` → `optional<int>` | `edge_glasses_get_battery` |
| `session_relax/meditate/focus/sleep(min)` | `edge_glasses_session_*` |
| `send_command(opcode, payload)` | `edge_glasses_send_command` |

### Feedback stream

| C++ | C | |
|---|---|---|
| `start_feedback_stream(hz)` | `edge_glasses_start_feedback_stream` | rate clamped 1–45 Hz |
| `feed(duty)` | `edge_stream_feed` | 0–100, proportional |
| `feed_reward(value)` | `edge_stream_feed_reward` | 0..1, 1 = in condition |
| `reward_event(duty, hold_ms)` | `edge_stream_reward_event` | discrete, immediate |
| `stop(clear)` | `edge_stream_stop` | clears the lens by default |

---

## Things that will bite you

Every one of these is enforced or handled by the SDK, but they matter when you
read the wire trace.

- **Every command is ≥ 2 bytes.** A 1-byte write is the *legacy opacity
  command*, so a bare `[0xA6]` is read as "opacity 166", not "start strobe".
  All builders pad argument-less opcodes to `[opcode, 0x00]`.
  `set_opacity()` is the one deliberate exception.
- **The firmware never NACKs.** Out-of-range arguments are silently clamped or
  dropped on the device, so a bad value fails *quietly*. Every builder clamps
  client-side, which is what makes "what you send is what runs" true.
- **Writes must never overlap.** Concurrent GATT writes to `0xFF01` fail
  outright on WinRT. `Glasses` serializes every write on an internal mutex, so
  your threads and the feedback stream cannot collide.
- **The glasses sleep on a timer.** Default 30 minutes from device *wake*, and
  `set_duration()` changes the total but does not restart the clock. Set it at
  session start.
- **The radio powers down after 2 minutes idle.** If a scan finds nothing, the
  user must tap the magnet on the temple. Surface that in your UX instead of
  retrying forever.
- **The lens freezes on disconnect.** A crashed app leaves the last tint in
  place until session expiry. Send `clear()` before an intentional disconnect,
  and consider `set_disconnect_behavior(true)` as a backstop.
- **A magnet tap overrides your app.** Gestures stay live while connected, so a
  tap mid-session cycles the standalone program out from under you.

Full detail: [protocol reference](../docs/bluetooth-protocol.md).

---

## Threading

`Glasses` is thread-safe: call it from anywhere. `FeedbackStream::feed()` is
lock-free and cheap enough for a DSP callback.

Calls **block** until the BLE write completes, which is what paces the stream.
Under `WinRtTransport` they block on the underlying WinRT async operation, so
call them from a worker thread — never a UI/STA thread.

---

## Testing

Three suites, no external test framework:

```bash
ctest --test-dir cpp-SDK/build --output-on-failure
```

Verified on two toolchains, so the SDK is not accidentally tied to either:

| Toolchain | Covers |
|---|---|
| MSVC 19.44 (VS 2022 Build Tools) + Windows SDK 10.0.22621, `/W4 /permissive-` | The whole library **including `winrt_transport.cpp`**, plus every test and example. Warning-clean. |
| GCC 15.2, `-Wall -Wextra -Wpedantic` | Everything except the WinRT transport, which is MSVC-only by nature. Warning-clean. |

| Test | What it covers |
|---|---|
| `cpp_sdk` | 220 checks: frame encoding, clamping at every boundary, the ≥ 2-byte rule, command sequences, presets, error paths, the fast-write decision, and FeedbackStream behaviour (coalescing, reward preemption, hold windows, retry-after-failure, concurrent feeds). |
| `c_api` | 90 checks, **compiled as C99** — which is also what proves `edge_glasses.h` is usable from a pure-C translation unit. |
| `golden_vectors` | Cross-language verification, below. |

### Cross-language verification

`tools/golden/gen_golden.py` drives the **reference Python SDK** through a fixed
script of 70 calls behind a fake BLE client and records all 95 resulting frames
to `tools/golden/golden_frames.txt`. The C++ and C# suites replay the same
script and assert byte-for-byte equality, including the
write-with/without-response decision.

So a divergence in clamping, padding, or byte order between any two language
bindings fails a build rather than reaching a customer's glasses.

```bash
python tools/golden/gen_golden.py --check   # verify the vectors are current
```

---

## Examples

| File | Shows |
|---|---|
| [`01_basic.cpp`](examples/01_basic.cpp) | Connect, session guard, battery, step the tint. |
| [`02_screen_dimmer.cpp`](examples/02_screen_dimmer.cpp) | The biofeedback pattern: smoothing, slew cap, fail-clear, proportional stream, discrete rewards. |
| [`03_breathe.cpp`](examples/03_breathe.cpp) | The on-board breathe engine, then fractional rates via boundary-latched `sync_breath`. |
| [`c_api_demo.c`](examples/c_api_demo.c) | Both C tiers — frame builders and the controller. Runs with no hardware. |

---

## License

MIT — see [LICENSE](LICENSE).
