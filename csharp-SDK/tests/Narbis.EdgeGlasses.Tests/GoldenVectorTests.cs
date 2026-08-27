// SPDX-License-Identifier: MIT
//
// Cross-language golden-vector check.
//
// Replays the exact call script from tools/golden/gen_golden.py through the C#
// SDK and asserts every frame is byte-identical to what the reference Python
// SDK produced. Any divergence in clamping, padding, byte order, or the
// write-with/without-response decision fails here rather than on a customer's
// glasses. The C++ SDK runs the same check (cpp-SDK/tests/golden_check.cpp).

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Xunit;

namespace Narbis.EdgeGlasses.Tests
{
    public class GoldenVectorTests
    {
        private sealed class GoldenFrame
        {
            public string Step { get; set; }

            public byte[] Bytes { get; set; }

            public bool WithResponse { get; set; }
        }

        /// <summary>Walk up from the test binary to the repository root.</summary>
        private static string FindGoldenFile()
        {
            var dir = new DirectoryInfo(AppContext.BaseDirectory);
            while (dir != null)
            {
                string candidate = Path.Combine(dir.FullName, "tools", "golden",
                    "golden_frames.txt");
                if (File.Exists(candidate))
                {
                    return candidate;
                }

                dir = dir.Parent;
            }

            throw new FileNotFoundException(
                "tools/golden/golden_frames.txt not found above " + AppContext.BaseDirectory +
                ". Regenerate it with: python tools/golden/gen_golden.py");
        }

        private static List<GoldenFrame> LoadGolden()
        {
            var frames = new List<GoldenFrame>();
            foreach (string raw in File.ReadAllLines(FindGoldenFile()))
            {
                string line = raw.TrimEnd('\r');
                if (line.Length == 0 || line[0] == '#')
                {
                    continue;
                }

                string[] parts = line.Split('\t');
                Assert.Equal(3, parts.Length);

                frames.Add(new GoldenFrame
                {
                    Step = parts[0],
                    Bytes = parts[1].Split(' ', StringSplitOptions.RemoveEmptyEntries)
                        .Select(b => byte.Parse(b, NumberStyles.HexNumber,
                            CultureInfo.InvariantCulture))
                        .ToArray(),
                    WithResponse = parts[2] != "N",
                });
            }

            Assert.NotEmpty(frames);
            return frames;
        }

        /// <summary>
        /// The shared call script, in the same order as gen_golden.py.
        /// </summary>
        private static List<(string Step, Func<Glasses, MockTransport, Task> Run)> BuildScript()
        {
            return new List<(string, Func<Glasses, MockTransport, Task>)>
            {
                // opacity — the legacy single-byte write
                ("opacity_0", (g, _) => g.SetOpacityAsync(0)),
                ("opacity_128", (g, _) => g.SetOpacityAsync(128)),
                ("opacity_255", (g, _) => g.SetOpacityAsync(255)),
                ("opacity_clamp_low", (g, _) => g.SetOpacityAsync(-1)),
                ("opacity_clamp_high", (g, _) => g.SetOpacityAsync(999)),
                ("clear", (g, _) => g.ClearAsync()),
                ("dark", (g, _) => g.DarkAsync()),

                // static duty
                ("static_0", (g, _) => g.SetStaticAsync(0)),
                ("static_50", (g, _) => g.SetStaticAsync(50)),
                ("static_100", (g, _) => g.SetStaticAsync(100)),
                ("static_clamp_low", (g, _) => g.SetStaticAsync(-5)),
                ("static_clamp_high", (g, _) => g.SetStaticAsync(255)),

                // parameters
                ("brightness_80", (g, _) => g.SetBrightnessAsync(80)),
                ("brightness_clamp_low", (g, _) => g.SetBrightnessAsync(-1)),
                ("brightness_clamp_high", (g, _) => g.SetBrightnessAsync(101)),
                ("duration_60", (g, _) => g.SetDurationAsync(60)),
                ("duration_clamp_low", (g, _) => g.SetDurationAsync(0)),
                ("duration_clamp_high", (g, _) => g.SetDurationAsync(120)),
                ("strobe_freq_10", (g, _) => g.SetStrobeFrequencyAsync(10)),
                ("strobe_freq_clamp_low", (g, _) => g.SetStrobeFrequencyAsync(0)),
                ("strobe_freq_clamp_high", (g, _) => g.SetStrobeFrequencyAsync(99)),
                ("strobe_duty_50", (g, _) => g.SetStrobeDutyAsync(50)),
                ("strobe_duty_clamp_low", (g, _) => g.SetStrobeDutyAsync(0)),
                ("strobe_duty_clamp_high", (g, _) => g.SetStrobeDutyAsync(100)),

                // lens config (fw >= 4.15.7)
                ("smoothing_0", (g, _) => g.SetLensSmoothingAsync(0)),
                ("smoothing_80", (g, _) => g.SetLensSmoothingAsync(80)),
                ("smoothing_85_truncates", (g, _) => g.SetLensSmoothingAsync(85)),
                ("smoothing_2550", (g, _) => g.SetLensSmoothingAsync(2550)),
                ("smoothing_clamp_high", (g, _) => g.SetLensSmoothingAsync(9999)),
                ("smoothing_clamp_low", (g, _) => g.SetLensSmoothingAsync(-50)),
                ("max_rate_5", (g, _) => g.SetLensMaxRateAsync(5)),
                ("max_rate_clamp_low", (g, _) => g.SetLensMaxRateAsync(-1)),
                ("max_rate_clamp_high", (g, _) => g.SetLensMaxRateAsync(200)),
                ("disconnect_freeze", (g, _) => g.SetDisconnectBehaviorAsync(false)),
                ("disconnect_fail_clear", (g, _) => g.SetDisconnectBehaviorAsync(true)),

                // modes
                ("strobe_start_bare", (g, _) => g.StartStrobeAsync()),
                ("strobe_start_full", (g, _) => g.StartStrobeAsync(10, 50)),
                ("strobe_start_duty_only", (g, _) => g.StartStrobeAsync(dutyPct: 40)),
                ("breathe_bare", (g, _) => g.StartBreatheAsync()),
                ("breathe_full", (g, _) => g.StartBreatheAsync(new BreatheOptions
                {
                    Bpm = 5,
                    InhalePct = 40,
                    HoldTopMs = 1000,
                    HoldBottomMs = 500,
                    Waveform = Waveform.Sine,
                })),
                ("breathe_linear", (g, _) => g.StartBreatheAsync(new BreatheOptions
                {
                    Waveform = Waveform.Linear,
                })),
                ("breathe_with_strobe", (g, _) => g.StartBreatheAsync(new BreatheOptions
                {
                    Bpm = 8,
                    WithStrobe = true,
                })),
                ("breathe_clamps", (g, _) => g.StartBreatheAsync(new BreatheOptions
                {
                    Bpm = 99,
                    InhalePct = 5,
                    HoldTopMs = 9999,
                })),

                // breathe sync (u16 little-endian cycle length)
                ("sync_5500", (g, _) => g.SyncBreathAsync(5500)),
                ("sync_5500_inhale50", (g, _) => g.SyncBreathAsync(5500, 50)),
                ("sync_10000", (g, _) => g.SyncBreathAsync(10000, 50)),
                ("sync_255", (g, _) => g.SyncBreathAsync(255)),
                ("sync_256", (g, _) => g.SyncBreathAsync(256)),
                ("sync_clamp_u16", (g, _) => g.SyncBreathAsync(70000)),
                ("sync_clamp_negative", (g, _) => g.SyncBreathAsync(-1)),
                ("sync_inhale_clamp_low", (g, _) => g.SyncBreathAsync(5000, 5)),
                ("sync_inhale_clamp_high", (g, _) => g.SyncBreathAsync(5000, 99)),

                // power / maintenance
                ("sleep", (g, _) => g.SleepAsync()),
                ("factory_reset", (g, _) => g.FactoryResetAsync()),

                // low-level escape hatch: the >= 2-byte rule
                ("cmd_no_payload", (g, _) => g.SendCommandAsync(0xA6)),
                ("cmd_sleep_no_payload", (g, _) => g.SendCommandAsync(0xA7)),
                ("cmd_with_payload", (g, _) => g.SendCommandAsync(0xA2, 80)),
                ("cmd_three_byte", (g, _) => g.SendCommandAsync(0xBA, 0x7C, 0x15, 40)),

                // preset sessions
                ("session_relax", (g, _) => g.SessionRelaxAsync(10)),
                ("session_meditate", (g, _) => g.SessionMeditateAsync(10)),
                ("session_focus", (g, _) => g.SessionFocusAsync(15)),
                ("session_sleep", (g, _) => g.SessionSleepAsync(15)),

                // streaming path: the response flag depends on fw >= 4.16.3
                ("stream_static_slow_0", Streaming(0, false)),
                ("stream_static_slow_42", Streaming(42, false)),
                ("stream_static_slow_100", Streaming(100, false)),
                ("stream_static_clamp_low", Streaming(-10, false)),
                ("stream_static_clamp_high", Streaming(500, false)),
                ("stream_static_fast_42", Streaming(42, true)),
                ("stream_static_fast_0", Streaming(0, true)),
            };
        }

        private static Func<Glasses, MockTransport, Task> Streaming(int duty, bool fastWrite)
        {
            return (g, t) =>
            {
                t.SupportsWriteWithoutResponse = fastWrite;
                return g.StreamStaticAsync(duty);
            };
        }

        [Fact]
        public async Task EveryFrameIsByteIdenticalToTheReferencePythonSdk()
        {
            List<GoldenFrame> expected = LoadGolden();

            var transport = new MockTransport();
            using var glasses = new Glasses(transport);

            var actual = new List<GoldenFrame>();
            foreach ((string step, Func<Glasses, MockTransport, Task> run) in BuildScript())
            {
                transport.TakeWrites();
                await run(glasses, transport);
                foreach (RecordedWrite write in transport.TakeWrites())
                {
                    actual.Add(new GoldenFrame
                    {
                        Step = step,
                        Bytes = write.Bytes,
                        WithResponse = write.WithResponse,
                    });
                }
            }

            Assert.Equal(expected.Count, actual.Count);

            for (int i = 0; i < expected.Count; i++)
            {
                GoldenFrame e = expected[i];
                GoldenFrame a = actual[i];
                Assert.True(e.Step == a.Step,
                    $"frame {i}: step '{a.Step}' does not line up with python's '{e.Step}'");
                Assert.True(e.Bytes.SequenceEqual(a.Bytes),
                    $"frame {i} ({e.Step}): python [{Hex(e.Bytes)}] vs C# [{Hex(a.Bytes)}]");
                Assert.True(e.WithResponse == a.WithResponse,
                    $"frame {i} ({e.Step}): python response flag {e.WithResponse}, C# {a.WithResponse}");
            }
        }

        private static string Hex(byte[] bytes)
        {
            return string.Join(" ", bytes.Select(b => b.ToString("X2", CultureInfo.InvariantCulture)));
        }
    }
}
