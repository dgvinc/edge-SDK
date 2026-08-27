#!/usr/bin/env python3
"""
Generate cross-language golden wire vectors from the reference Python SDK.

The Python SDK (``python-SDK/edge_glasses``) is the reference implementation of
the Narbis Edge wire protocol. This script drives the *real* ``Glasses`` class
through a fixed script of calls behind a fake BLE client, records every byte it
puts on the control characteristic, and writes them to ``golden_frames.txt``.

The C++ and C# SDKs replay the same script and assert byte-for-byte equality
(``cpp-SDK/tests/golden_check.cpp``, ``csharp-SDK/tests/GoldenVectorTests.cs``),
so a divergence in any binding's clamping, padding, or byte order fails a build
rather than reaching a customer's glasses.

Usage:
    python tools/golden/gen_golden.py            # regenerate golden_frames.txt
    python tools/golden/gen_golden.py --check    # verify it is up to date

Output format — one frame per line, blank lines and '#' comments ignored:
    <step-name><TAB><hex bytes, space separated><TAB>R|N
where R = write-with-response, N = write-without-response.
"""

import argparse
import asyncio
import os
import sys
import types

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden_frames.txt")


def _install_bleak_stub():
    """Satisfy ``edge_glasses``'s import of bleak without installing it.

    Only the names glasses.py imports are needed; none of them are called,
    because the script below substitutes a fake client object.
    """
    if "bleak" in sys.modules:
        return

    bleak = types.ModuleType("bleak")

    class BleakClient:  # pragma: no cover - never instantiated
        def __init__(self, *a, **k):
            raise RuntimeError("stub")

    class BleakScanner:  # pragma: no cover - never instantiated
        pass

    bleak.BleakClient = BleakClient
    bleak.BleakScanner = BleakScanner

    exc = types.ModuleType("bleak.exc")

    class BleakError(Exception):
        pass

    exc.BleakError = BleakError
    bleak.exc = exc

    sys.modules["bleak"] = bleak
    sys.modules["bleak.exc"] = exc


class FakeClient:
    """Stands in for a connected BleakClient, recording control writes."""

    def __init__(self, sink):
        self._sink = sink

    async def write_gatt_char(self, uuid, data, response=True):
        self._sink.append((bytes(data), bool(response)))


async def build_vectors():
    _install_bleak_stub()
    sys.path.insert(0, os.path.join(REPO_ROOT, "python-SDK"))
    from edge_glasses import Glasses, Waveform  # noqa: E402

    frames = []          # [(step_name, bytes, with_response)]
    writes = []          # filled by FakeClient

    glasses = Glasses(address="00:00:00:00:00:00")
    glasses._client = FakeClient(writes)
    glasses._connected = True

    async def step(name, coro, fast_write=False):
        """Run one SDK call and label every frame it emitted."""
        glasses._ctrl_supports_wnr = fast_write
        writes.clear()
        await coro
        for data, response in writes:
            frames.append((name, data, response))

    # -- opacity: the legacy single-byte write -------------------------------
    await step("opacity_0", glasses.set_opacity(0))
    await step("opacity_128", glasses.set_opacity(128))
    await step("opacity_255", glasses.set_opacity(255))
    await step("opacity_clamp_low", glasses.set_opacity(-1))
    await step("opacity_clamp_high", glasses.set_opacity(999))
    await step("clear", glasses.clear())
    await step("dark", glasses.dark())

    # -- static duty ---------------------------------------------------------
    await step("static_0", glasses.set_static(0))
    await step("static_50", glasses.set_static(50))
    await step("static_100", glasses.set_static(100))
    await step("static_clamp_low", glasses.set_static(-5))
    await step("static_clamp_high", glasses.set_static(255))

    # -- parameters ----------------------------------------------------------
    await step("brightness_80", glasses.set_brightness(80))
    await step("brightness_clamp_low", glasses.set_brightness(-1))
    await step("brightness_clamp_high", glasses.set_brightness(101))
    await step("duration_60", glasses.set_duration(60))
    await step("duration_clamp_low", glasses.set_duration(0))
    await step("duration_clamp_high", glasses.set_duration(120))
    await step("strobe_freq_10", glasses.set_strobe_frequency(10))
    await step("strobe_freq_clamp_low", glasses.set_strobe_frequency(0))
    await step("strobe_freq_clamp_high", glasses.set_strobe_frequency(99))
    await step("strobe_duty_50", glasses.set_strobe_duty(50))
    await step("strobe_duty_clamp_low", glasses.set_strobe_duty(0))
    await step("strobe_duty_clamp_high", glasses.set_strobe_duty(100))

    # -- lens config (fw >= 4.15.7) ------------------------------------------
    await step("smoothing_0", glasses.set_lens_smoothing(0))
    await step("smoothing_80", glasses.set_lens_smoothing(80))
    await step("smoothing_85_truncates", glasses.set_lens_smoothing(85))
    await step("smoothing_2550", glasses.set_lens_smoothing(2550))
    await step("smoothing_clamp_high", glasses.set_lens_smoothing(9999))
    await step("smoothing_clamp_low", glasses.set_lens_smoothing(-50))
    await step("max_rate_5", glasses.set_lens_max_rate(5))
    await step("max_rate_clamp_low", glasses.set_lens_max_rate(-1))
    await step("max_rate_clamp_high", glasses.set_lens_max_rate(200))
    await step("disconnect_freeze", glasses.set_disconnect_behavior(False))
    await step("disconnect_fail_clear", glasses.set_disconnect_behavior(True))

    # -- modes ---------------------------------------------------------------
    await step("strobe_start_bare", glasses.start_strobe())
    await step("strobe_start_full", glasses.start_strobe(hz=10, duty_pct=50))
    await step("strobe_start_duty_only", glasses.start_strobe(duty_pct=40))
    await step("breathe_bare", glasses.start_breathe())
    await step(
        "breathe_full",
        glasses.start_breathe(
            bpm=5,
            inhale_pct=40,
            hold_top_ms=1000,
            hold_bottom_ms=500,
            waveform=Waveform.SINE,
        ),
    )
    await step("breathe_linear", glasses.start_breathe(waveform=Waveform.LINEAR))
    await step("breathe_with_strobe", glasses.start_breathe(bpm=8, with_strobe=True))
    await step("breathe_clamps", glasses.start_breathe(bpm=99, inhale_pct=5, hold_top_ms=9999))

    # -- breathe sync (u16 little-endian cycle length) ------------------------
    await step("sync_5500", glasses.sync_breath(5500))
    await step("sync_5500_inhale50", glasses.sync_breath(5500, inhale_pct=50))
    await step("sync_10000", glasses.sync_breath(10000, inhale_pct=50))
    await step("sync_255", glasses.sync_breath(255))
    await step("sync_256", glasses.sync_breath(256))
    await step("sync_clamp_u16", glasses.sync_breath(70000))
    await step("sync_clamp_negative", glasses.sync_breath(-1))
    await step("sync_inhale_clamp_low", glasses.sync_breath(5000, inhale_pct=5))
    await step("sync_inhale_clamp_high", glasses.sync_breath(5000, inhale_pct=99))

    # -- power / maintenance -------------------------------------------------
    await step("sleep", glasses.sleep())
    await step("factory_reset", glasses.factory_reset())

    # -- low-level escape hatch: the >= 2-byte rule --------------------------
    await step("cmd_no_payload", glasses.send_command(0xA6))
    await step("cmd_sleep_no_payload", glasses.send_command(0xA7))
    await step("cmd_with_payload", glasses.send_command(0xA2, bytes([80])))
    await step("cmd_three_byte", glasses.send_command(0xBA, bytes([0x7C, 0x15, 40])))

    # -- preset sessions -----------------------------------------------------
    await step("session_relax", glasses.session_relax(10))
    await step("session_meditate", glasses.session_meditate(10))
    await step("session_focus", glasses.session_focus(15))
    await step("session_sleep", glasses.session_sleep(15))

    # -- streaming path: response flag depends on fw >= 4.16.3 ---------------
    await step("stream_static_slow_0", glasses._stream_static(0), fast_write=False)
    await step("stream_static_slow_42", glasses._stream_static(42), fast_write=False)
    await step("stream_static_slow_100", glasses._stream_static(100), fast_write=False)
    await step("stream_static_clamp_low", glasses._stream_static(-10), fast_write=False)
    await step("stream_static_clamp_high", glasses._stream_static(500), fast_write=False)
    await step("stream_static_fast_42", glasses._stream_static(42), fast_write=True)
    await step("stream_static_fast_0", glasses._stream_static(0), fast_write=True)

    return frames


def render(frames):
    lines = [
        "# EDGE Glasses — golden wire vectors",
        "# Generated by tools/golden/gen_golden.py from the reference Python SDK.",
        "# Format: <step><TAB><hex bytes><TAB>R (with response) | N (without response)",
        "# Do not edit by hand — regenerate instead.",
        "",
    ]
    for name, data, response in frames:
        hexed = " ".join("%02X" % b for b in data)
        lines.append("%s\t%s\t%s" % (name, hexed, "R" if response else "N"))
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="fail if golden_frames.txt is out of date")
    args = parser.parse_args()

    frames = asyncio.run(build_vectors())
    text = render(frames)

    if args.check:
        if not os.path.exists(OUT_PATH):
            print("golden_frames.txt missing - run without --check", file=sys.stderr)
            return 1
        with open(OUT_PATH, "r", encoding="utf-8") as f:
            current = f.read()
        if current != text:
            print("golden_frames.txt is out of date - regenerate it", file=sys.stderr)
            return 1
        print("golden_frames.txt is up to date (%d frames)" % len(frames))
        return 0

    with open(OUT_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote %s (%d frames)" % (OUT_PATH, len(frames)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
