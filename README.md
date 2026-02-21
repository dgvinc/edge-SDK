# EDGE Smart Glasses

Open-source smart LCD glasses for meditation, neurofeedback, and biofeedback applications.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-ESP32-green.svg)
![Python](https://img.shields.io/badge/python-3.8+-blue.svg)
![TypeScript](https://img.shields.io/badge/typescript-5.0+-blue.svg)

## What is EDGE?

EDGE glasses feature LCD lenses that dynamically change opacity via Bluetooth. An open platform for biofeedback, neurofeedback, and human-computer interaction research.

### Applications

| Domain | Use Case |
|--------|----------|
| **Meditation** | Guided breathing with visual pacing and feedback |
| **EEG Neurofeedback** | Alpha/theta training, focus enhancement, relaxation |
| **HRV Biofeedback** | Heart rate variability coherence training |
| **EMG Biofeedback** | Muscle tension awareness, relaxation training |
| **EOG Integration** | Eye movement-triggered states, blink detection |
| **fNIRS** | Hemodynamic response feedback, cognitive load |
| **rPPG** | Camera-based heart rate, stress monitoring |
| **GSR/EDA** | Arousal-based feedback, stress response |
| **Respiration** | Breathing rate entrainment, paced breathing |
| **BCI Research** | Motor imagery, SSVEP, P300 paradigms |

### Why EDGE?

- **Open Protocol** — Simple BLE API, no vendor lock-in
- **Low Latency** — 20+ Hz update rate for real-time feedback
- **Cross-Platform SDKs** — Python for research, JS for web apps
- **Sensor Agnostic** — Works with any biosignal source via LSL/brainflow
- **Research Ready** — Compatible with OpenBCI, Muse, Polar, and lab equipment

## Hardware

| Component | Specification |
|-----------|---------------|
| MCU | ESP32-PICO-D4 |
| Connectivity | Bluetooth Low Energy 4.0+ |
| Lens Control | PWM-driven LCD opacity |
| Power | Li-ion battery, ~8hr active |
| Sleep Current | ~16 µA |

## Repositories

| Repo | Description |
|------|-------------|
| [firmware](https://github.com/edge-glasses/firmware) | ESP32 firmware with BLE GATT server |
| [python-sdk](https://github.com/edge-glasses/python-sdk) | Python SDK with OpenBCI/Muse/Polar examples |
| [js-sdk](https://github.com/edge-glasses/js-sdk) | JavaScript/TypeScript SDK for web apps |

## Quick Start

### Python
```bash
pip install edge-glasses
```

```python
from edge_glasses import Glasses
import asyncio

async def main():
    async with Glasses() as glasses:
        await glasses.set_opacity(128)        # 50% dark
        await glasses.session_meditate(10)    # 10-min session

asyncio.run(main())
```

### JavaScript
```bash
npm install edge-glasses
```

```typescript
import { Glasses } from 'edge-glasses';

const glasses = new Glasses();
await glasses.connect();
await glasses.setOpacity(128);
await glasses.sessionMeditate(10);
```

## Integrations

Works with popular biosignal platforms and research equipment. Your computer runs the SDK, which bridges between sensor data (LSL/BLE/USB) and the glasses (BLE).

```
┌─────────────┐      LSL/USB/BLE      ┌─────────────┐       BLE        ┌─────────────┐
│   Sensor    │ ──────────────────▶   │  Computer   │ ───────────────▶ │   Glasses   │
│ (EEG/HRV/…) │                       │ (Python SDK)│                  │   (ESP32)   │
└─────────────┘                       └─────────────┘                  └─────────────┘
```

### Consumer Devices
| Platform | Signals | Connection |
|----------|---------|------------|
| **OpenBCI** | EEG, EMG, ECG, EOG | Cyton, Ganglion via brainflow |
| **Muse** | EEG (4-ch) | Muse 2, Muse S via muselsl |
| **Polar** | HR, HRV | H10, H9, Verity Sense via BLE |
| **Neurosity** | EEG (8-ch) | Crown via brainflow |
| **BrainBit** | EEG (4-ch) | Via brainflow |

### Research Equipment
| Platform | Signals | Connection |
|----------|---------|------------|
| **LSL** | Any | Lab Streaming Layer protocol |
| **BrainFlow** | EEG, EMG, PPG | 20+ supported boards |
| **NIRx** | fNIRS | Via LSL |
| **Biopac** | EMG, ECG, GSR, Resp | Via LSL |
| **Tobii** | Eye tracking, EOG | Via LSL |

### Examples
| Example | Description |
|---------|-------------|
| [openbci_feedback.py](https://github.com/edge-glasses/python-sdk/blob/main/examples/openbci_feedback.py) | EEG alpha neurofeedback |
| [muse_eeg.py](https://github.com/edge-glasses/python-sdk/blob/main/examples/muse_eeg.py) | Meditation/focus training |
| [polar_hrv.py](https://github.com/edge-glasses/python-sdk/blob/main/examples/polar_hrv.py) | HRV coherence training |
| [lsl_integration.py](https://github.com/edge-glasses/python-sdk/blob/main/examples/lsl_integration.py) | Any LSL-compatible source |
| [Integration Guide](https://github.com/edge-glasses/python-sdk/blob/main/docs/INTEGRATION_GUIDE.md) | Full setup documentation |

## BLE Protocol

Simple byte-based protocol for direct integration:

| Command | Bytes | Description |
|---------|-------|-------------|
| Opacity | `[0x00-0xFF]` | Set lens opacity (single byte) |
| Strobe | `[0xA1, start, end]` | Set frequency range 1-50 Hz |
| Brightness | `[0xA2, percent]` | Set max brightness 0-100% |
| Breathing | `[0xA3, inh, h_in, exh, h_out]` | Set breathing pattern |
| Duration | `[0xA4, minutes]` | Set session length |
| Hold | `[0xA5, duty]` | Static hold at duty % |
| Resume | `[0xA6]` | Restart session |
| Sleep | `[0xA7]` | Enter deep sleep |

Full protocol: [API Reference](https://github.com/edge-glasses/firmware/blob/main/docs/API_REFERENCE.md)

## Features

### Timed Sessions
Glasses run autonomous meditation sessions with:
- Strobe frequency that slows over time (e.g., 12→8 Hz)
- Breathing pattern with growing hold times
- Auto-sleep when session completes

### Preset Programs
| Preset | Strobe | Breathing | Best For |
|--------|--------|-----------|----------|
| Relax | 10→4 Hz | 5s cycles | Stress relief, wind-down |
| Focus | 15→10 Hz | 3s cycles | Concentration, study |
| Meditate | 12→8 Hz | 4s cycles | General practice |
| Sleep | 6→2 Hz | 6s cycles | Pre-sleep routine |

### Real-time Control
Update opacity at 20+ Hz for smooth neurofeedback:
```python
while True:
    alpha = get_eeg_alpha()  # Your processing
    await glasses.set_opacity(int(alpha * 255))
    await asyncio.sleep(0.05)
```

## Documentation

- [API Reference](https://github.com/edge-glasses/firmware/blob/main/docs/API_REFERENCE.md) — Complete BLE protocol
- [Integration Guide](https://github.com/edge-glasses/python-sdk/blob/main/docs/INTEGRATION_GUIDE.md) — OpenBCI, Muse, Polar, LSL setup
- [Python SDK Docs](https://github.com/edge-glasses/python-sdk#readme)
- [JavaScript SDK Docs](https://github.com/edge-glasses/js-sdk#readme)

## Community

- **Issues** — Report bugs or request features in the relevant repo
- **Discussions** — Share projects, ask questions
- **OpenBCI Discord** — Find us in the #hardware channel

## License

MIT License — free for personal and commercial use.

## Contributing

Contributions welcome! See individual repos for contribution guidelines.

---

**Built for the neurofeedback community** 🧠
