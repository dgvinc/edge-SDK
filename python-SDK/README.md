# EDGE Glasses Python SDK

Control EDGE Smart LCD Glasses over Bluetooth Low Energy.

## Installation

```bash
pip install edge-glasses
```

## Quick Start

```python
import asyncio
from edge_glasses import Glasses

async def main():
    # Connect and set opacity
    async with Glasses() as glasses:
        await glasses.set_opacity(128)  # 50% dark

asyncio.run(main())
```

## Features

- Simple opacity control (0-255)
- Timed meditation sessions with breathing patterns
- Strobe frequency control (1-50 Hz)
- Preset sessions (relax, focus, meditate, sleep)
- Async/await API using `bleak` BLE library
- Cross-platform (Windows, macOS, Linux, Raspberry Pi)

## Integrations

Works natively with OpenBCI, brainflow, LSL, and any BLE pipeline.

| Platform | Example | Description |
|----------|---------|-------------|
| **OpenBCI** | `examples/openbci_feedback.py` | EEG neurofeedback via brainflow |
| **Muse** | `examples/muse_eeg.py` | Meditation/focus training |
| **Polar** | `examples/polar_hrv.py` | HRV coherence training |
| **LSL** | `examples/lsl_integration.py` | Lab Streaming Layer bridge |
| **HRV** | `examples/hrv_breathing.py` | Heart rate variability training |

See `docs/INTEGRATION_GUIDE.md` for complete integration documentation.

## Usage

### Basic Control

```python
from edge_glasses import Glasses
import asyncio

async def main():
    glasses = Glasses()
    await glasses.connect()
    
    # Simple opacity control
    await glasses.clear()           # Fully transparent
    await glasses.set_opacity(128)  # 50% dark
    await glasses.dark()            # Fully opaque
    
    # Static hold at specific duty cycle
    await glasses.hold(75)          # Hold at 75%
    
    # Sleep the device
    await glasses.sleep()
    
    await glasses.disconnect()

asyncio.run(main())
```

### Timed Sessions

```python
async def meditation():
    async with Glasses() as glasses:
        # Start a 10-minute session
        await glasses.start_session(
            duration=10,          # 10 minutes
            strobe_start=12,      # Start at 12 Hz
            strobe_end=8,         # End at 8 Hz
            inhale=4.0,           # 4s inhale
            hold_in_end=4.0,      # Hold grows to 4s
            exhale=4.0,           # 4s exhale
            hold_out_end=4.0,     # Hold grows to 4s
            brightness=100        # Full brightness
        )
        # Session runs automatically, device sleeps when done
```

### Preset Sessions

```python
async def presets():
    async with Glasses() as glasses:
        # Relaxation - slower frequencies, longer breathing
        await glasses.session_relax(duration=15)
        
        # Focus - higher frequencies, shorter cycles
        await glasses.session_focus(duration=10)
        
        # Standard meditation
        await glasses.session_meditate(duration=10)
        
        # Sleep preparation - very slow, device auto-sleeps after
        await glasses.session_sleep(duration=20)
```

### Scanning for Devices

```python
async def find_devices():
    # Scan for EDGE Glasses
    devices = await Glasses.scan(timeout=5.0)
    
    for device in devices:
        print(f"{device.name} - {device.address} (RSSI: {device.rssi})")
    
    # Connect to specific device
    if devices:
        glasses = Glasses(address=devices[0].address)
        await glasses.connect()
```

### Real-time Control (Research/Neurofeedback)

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
            
            await asyncio.sleep(0.05)  # 20 Hz update rate
```

## API Reference

### Connection

| Method | Description |
|--------|-------------|
| `Glasses(address=None)` | Create controller. Auto-scans if no address. |
| `await glasses.connect()` | Connect to device |
| `await glasses.disconnect()` | Disconnect from device |
| `await Glasses.scan(timeout=5.0)` | Scan for devices |

### Simple Control

| Method | Description |
|--------|-------------|
| `await glasses.set_opacity(0-255)` | Set lens opacity |
| `await glasses.clear()` | Fully transparent |
| `await glasses.dark()` | Fully opaque |
| `await glasses.hold(0-100)` | Static hold at duty % |
| `await glasses.sleep()` | Enter deep sleep |

### Session Control

| Method | Description |
|--------|-------------|
| `await glasses.start_session(...)` | Configure and start session |
| `await glasses.set_strobe(start, end)` | Set frequency range (Hz) |
| `await glasses.set_breathing(inh, hold_in, exh, hold_out)` | Set breathing pattern |
| `await glasses.set_duration(minutes)` | Set session length |
| `await glasses.set_brightness(0-100)` | Set max brightness |
| `await glasses.resume()` | Restart session |

### Preset Sessions

| Method | Description |
|--------|-------------|
| `await glasses.session_relax(duration)` | Relaxation preset |
| `await glasses.session_focus(duration)` | Focus/concentration preset |
| `await glasses.session_meditate(duration)` | Meditation preset |
| `await glasses.session_sleep(duration)` | Sleep preparation preset |

## Session Parameters Explained

### Strobe Frequency
The strobe frequency (Hz) controls how fast the lenses flash. Lower frequencies (4-8 Hz) are more relaxing, higher frequencies (10-15 Hz) are more alerting.

### Breathing Pattern
Each breath cycle has 4 phases:
1. **Inhale**: Lenses transition clear → dark
2. **Hold In**: Lenses stay dark (duration grows over session)
3. **Exhale**: Lenses transition dark → clear
4. **Hold Out**: Lenses stay clear (duration grows over session)

The hold durations start at 0 and grow linearly to their end values over the session duration.

## OpenBCI Integration

```python
from edge_glasses import Glasses
from brainflow import BoardShim, BrainFlowInputParams
import asyncio

async def openbci_feedback():
    # Setup OpenBCI
    params = BrainFlowInputParams()
    params.serial_port = '/dev/ttyUSB0'
    board = BoardShim(0, params)  # Cyton
    board.prepare_session()
    board.start_stream()
    
    async with Glasses() as glasses:
        while True:
            # Get EEG data
            data = board.get_current_board_data(250)
            
            # Calculate alpha power (your processing)
            alpha = calculate_alpha_power(data)
            
            # Map to glasses
            opacity = int(min(255, alpha * 2.55))
            await glasses.set_opacity(opacity)
            
            await asyncio.sleep(0.1)

asyncio.run(openbci_feedback())
```

## Troubleshooting

### Device not found
- Ensure glasses are powered on (arms open)
- Check Bluetooth is enabled
- Move closer to device
- Try `await Glasses.scan()` to verify detection

### Connection fails
- Disconnect from other apps (nRF Connect, etc.)
- Restart Bluetooth
- Check battery level on glasses

### Commands not working
- Verify connection with `glasses.is_connected`
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

- [API Reference](https://github.com/edge-glasses/firmware/blob/main/docs/API_REFERENCE.md)
- [Integration Guide](https://github.com/edge-glasses/python-sdk/blob/main/docs/INTEGRATION_GUIDE.md)
- [Hardware Documentation](https://github.com/edge-glasses/firmware)
- [JavaScript SDK](https://github.com/edge-glasses/js-sdk)
