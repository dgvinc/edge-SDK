# EDGE Glasses Integration Guide

Complete guide for integrating EDGE Smart Glasses with neurofeedback and biofeedback systems.

---

## Table of Contents

1. [Overview](#overview)
2. [OpenBCI Integration](#openbci-integration)
3. [Muse Integration](#muse-integration)
4. [Polar HR Integration](#polar-hr-integration)
5. [LSL Pipeline Integration](#lsl-pipeline-integration)
6. [BrainFlow Integration](#brainflow-integration)
7. [Custom Integration](#custom-integration)

---

## Overview

EDGE Glasses can be controlled from any system that can send BLE commands. The SDK provides:

| SDK | Language | Use Case |
|-----|----------|----------|
| **Python** | Python 3.8+ | Research, OpenBCI, scripting |
| **JavaScript** | JS/TS | Web apps, React, browser-based |

### Basic Integration Pattern

```
[Sensor] → [Processing] → [Control Signal] → [EDGE Glasses]
   │            │               │                  │
   EEG         FFT           0-255            BLE Write
   HR          HRV           0-100%           opacity
   etc.      Bands          normalized
```

---

## OpenBCI Integration

### Hardware
- OpenBCI Cyton (8-channel)
- OpenBCI Ganglion (4-channel)
- OpenBCI Cyton + Daisy (16-channel)

### Software Requirements
```bash
pip install edge-glasses brainflow numpy
```

### Basic Example

```python
import asyncio
import numpy as np
from brainflow.board_shim import BoardShim, BrainFlowInputParams, BoardIds
from brainflow.data_filter import DataFilter, FilterTypes
from edge_glasses import Glasses

async def openbci_feedback():
    # Setup OpenBCI
    params = BrainFlowInputParams()
    params.serial_port = '/dev/ttyUSB0'  # Windows: 'COM3'
    
    board = BoardShim(BoardIds.CYTON_BOARD, params)
    board.prepare_session()
    board.start_stream()
    
    # Connect glasses
    glasses = Glasses()
    await glasses.connect()
    
    try:
        while True:
            # Get 1 second of data
            data = board.get_current_board_data(250)
            
            if data.shape[1] >= 250:
                # Get EEG channel (Oz recommended for alpha)
                eeg_channels = BoardShim.get_eeg_channels(BoardIds.CYTON_BOARD)
                channel = data[eeg_channels[0]]
                
                # Calculate alpha power (8-12 Hz)
                DataFilter.perform_bandpass(
                    channel, 250, 8.0, 12.0, 4,
                    FilterTypes.BUTTERWORTH, 0
                )
                alpha_power = np.sqrt(np.mean(channel ** 2))
                
                # Normalize and send to glasses
                normalized = min(1.0, alpha_power / 50.0)  # Adjust divisor
                opacity = int(normalized * 255)
                await glasses.set_opacity(opacity)
            
            await asyncio.sleep(0.1)
    
    finally:
        board.stop_stream()
        board.release_session()
        await glasses.disconnect()

asyncio.run(openbci_feedback())
```

### LSL Output from OpenBCI GUI

If using OpenBCI GUI:
1. Start OpenBCI GUI
2. Enable "Networking" → "LSL"
3. Use our LSL integration example

---

## Muse Integration

### Hardware
- Muse 2, Muse S, Muse S (Gen 2)

### Software Requirements
```bash
pip install edge-glasses muselsl pylsl mne scipy
```

### Step 1: Start Muse Stream

```bash
# In terminal 1
muselsl stream
```

### Step 2: Connect and Process

```python
import asyncio
from pylsl import StreamInlet, resolve_byprop
from edge_glasses import Glasses

async def muse_feedback():
    # Find Muse stream
    streams = resolve_byprop('type', 'EEG', timeout=10)
    inlet = StreamInlet(streams[0])
    
    # Connect glasses
    glasses = Glasses()
    await glasses.connect()
    
    # Process loop
    alpha_buffer = []
    
    while True:
        sample, _ = inlet.pull_sample(timeout=0.1)
        if sample:
            # Simple alpha estimation (channel average)
            alpha_buffer.append(abs(sum(sample[:4]) / 4))
            
            if len(alpha_buffer) >= 256:  # 1 second
                alpha_power = sum(alpha_buffer) / len(alpha_buffer)
                normalized = min(1.0, alpha_power / 100.0)
                opacity = int(normalized * 255)
                await glasses.set_opacity(opacity)
                alpha_buffer = alpha_buffer[128:]  # 50% overlap
        
        await asyncio.sleep(0.004)  # ~256 Hz

asyncio.run(muse_feedback())
```

---

## Polar HR Integration

### Hardware
- Polar H10 (recommended)
- Polar H9
- Polar Verity Sense

### Software Requirements
```bash
pip install edge-glasses bleak
```

### HRV Coherence Training

```python
import asyncio
import struct
from bleak import BleakClient, BleakScanner
from edge_glasses import Glasses

HR_CHAR_UUID = "00002a37-0000-1000-8000-00805f9b34fb"

class HRVTrainer:
    def __init__(self):
        self.glasses = None
        self.hr_client = None
        self.rr_intervals = []
    
    async def connect(self):
        # Find Polar device
        devices = await BleakScanner.discover()
        polar = next((d for d in devices if d.name and "Polar" in d.name), None)
        
        if not polar:
            raise RuntimeError("No Polar device found")
        
        # Connect HR monitor
        self.hr_client = BleakClient(polar.address)
        await self.hr_client.connect()
        await self.hr_client.start_notify(HR_CHAR_UUID, self._hr_callback)
        
        # Connect glasses
        self.glasses = Glasses()
        await self.glasses.connect()
    
    def _hr_callback(self, sender, data):
        # Parse RR intervals
        flags = data[0]
        if (flags >> 4) & 0x01:  # RR present
            offset = 2 if not (flags & 0x01) else 3
            while offset + 1 < len(data):
                rr = struct.unpack('<H', data[offset:offset+2])[0]
                self.rr_intervals.append(rr * 1000 / 1024)
                offset += 2
    
    def calculate_coherence(self):
        if len(self.rr_intervals) < 10:
            return 0.5
        
        import numpy as np
        rr = np.array(self.rr_intervals[-60:])
        rmssd = np.sqrt(np.mean(np.diff(rr) ** 2))
        return min(1.0, rmssd / 100.0)
    
    async def run(self, duration=60):
        start = asyncio.get_event_loop().time()
        
        while (asyncio.get_event_loop().time() - start) < duration:
            coherence = self.calculate_coherence()
            opacity = int(coherence * 255)
            await self.glasses.set_opacity(opacity)
            await asyncio.sleep(0.5)

asyncio.run(HRVTrainer().connect())
```

---

## LSL Pipeline Integration

Lab Streaming Layer (LSL) is the standard for neuroscience data streaming.

### Compatible Software
- OpenBCI GUI
- BCI2000
- OpenViBE
- NeuroPype
- BCILAB
- Any LSL-enabled software

### EDGE Glasses as LSL Device

```python
from pylsl import StreamInfo, StreamOutlet, StreamInlet, resolve_stream
from edge_glasses import Glasses
import asyncio

class GlassesLSLBridge:
    def __init__(self):
        self.glasses = None
        self.outlet = None
        self.inlet = None
    
    async def setup(self):
        # Connect glasses
        self.glasses = Glasses()
        await self.glasses.connect()
        
        # Create outlet (publish state)
        info = StreamInfo('EDGE_Glasses', 'Markers', 1, 10, 'float32', 'edge001')
        self.outlet = StreamOutlet(info)
        
        # Find control stream (receive commands)
        streams = resolve_stream('name', 'GlassesControl', timeout=2)
        if streams:
            self.inlet = StreamInlet(streams[0])
    
    async def run(self):
        while True:
            # Check for incoming commands
            if self.inlet:
                sample, _ = self.inlet.pull_sample(timeout=0.0)
                if sample:
                    opacity = int(sample[0] * 255)
                    await self.glasses.set_opacity(opacity)
            
            # Publish current state
            # self.outlet.push_sample([current_opacity])
            
            await asyncio.sleep(0.1)
```

---

## BrainFlow Integration

BrainFlow provides a unified API for many EEG devices.

### Supported Boards
- OpenBCI (Cyton, Ganglion, Cyton+Daisy)
- Muse
- Neurosity Crown
- BrainBit
- And many more

### Universal Example

```python
from brainflow.board_shim import BoardShim, BrainFlowInputParams, BoardIds
from brainflow.data_filter import DataFilter, FilterTypes, AggOperations
from edge_glasses import Glasses
import asyncio
import numpy as np

async def brainflow_universal(board_id: int, params: BrainFlowInputParams):
    """Works with any BrainFlow-supported board"""
    
    board = BoardShim(board_id, params)
    board.prepare_session()
    board.start_stream()
    
    glasses = Glasses()
    await glasses.connect()
    
    sample_rate = BoardShim.get_sampling_rate(board_id)
    eeg_channels = BoardShim.get_eeg_channels(board_id)
    
    try:
        while True:
            data = board.get_current_board_data(sample_rate)
            
            if data.shape[1] >= sample_rate:
                # Average alpha across all EEG channels
                alpha_powers = []
                
                for ch in eeg_channels:
                    channel = data[ch].copy()
                    
                    # Bandpass 8-12 Hz
                    DataFilter.perform_bandpass(
                        channel, sample_rate,
                        8.0, 12.0, 4,
                        FilterTypes.BUTTERWORTH, 0
                    )
                    
                    alpha_powers.append(np.sqrt(np.mean(channel ** 2)))
                
                avg_alpha = np.mean(alpha_powers)
                normalized = min(1.0, avg_alpha / 50.0)
                
                await glasses.set_opacity(int(normalized * 255))
            
            await asyncio.sleep(0.1)
    
    finally:
        board.stop_stream()
        board.release_session()
        await glasses.disconnect()

# OpenBCI Cyton
params = BrainFlowInputParams()
params.serial_port = '/dev/ttyUSB0'
asyncio.run(brainflow_universal(BoardIds.CYTON_BOARD, params))

# Muse 2
params = BrainFlowInputParams()
params.serial_port = ''  # Uses BLE
asyncio.run(brainflow_universal(BoardIds.MUSE_2_BOARD, params))
```

---

## Custom Integration

### Direct BLE Control

Any system that can write BLE GATT characteristics can control the glasses.

#### Connection Info
| Parameter | Value |
|-----------|-------|
| Device Name | `Smart_Glasses` |
| Service UUID | `0x00FF` |
| Characteristic UUID | `0xFF01` |

#### Command Reference
| Bytes | Action |
|-------|--------|
| `[0x00-0xFF]` | Set opacity (single byte) |
| `[0xA1, start, end]` | Set strobe Hz range |
| `[0xA2, percent]` | Set brightness |
| `[0xA3, inh, h_in, exh, h_out]` | Set breathing |
| `[0xA4, minutes]` | Set session duration |
| `[0xA5, duty]` | Static hold |
| `[0xA6]` | Resume session |
| `[0xA7]` | Sleep |

### Example: Node.js with noble

```javascript
const noble = require('@abandonware/noble');

const SERVICE_UUID = '00ff';
const CHAR_UUID = 'ff01';

noble.on('discover', async (peripheral) => {
  if (peripheral.advertisement.localName === 'Smart_Glasses') {
    await peripheral.connectAsync();
    
    const { characteristics } = await peripheral.discoverSomeServicesAndCharacteristicsAsync(
      [SERVICE_UUID], [CHAR_UUID]
    );
    
    const char = characteristics[0];
    
    // Set opacity
    await char.writeAsync(Buffer.from([128]), true);  // 50%
    
    // Start session
    await char.writeAsync(Buffer.from([0xA4, 10]), true);  // 10 min
  }
});

noble.startScanningAsync();
```

### Example: C# with InTheHand.BluetoothLE

```csharp
using InTheHand.Bluetooth;

var device = await BluetoothDevice.FromIdAsync("Smart_Glasses");
var service = await device.Gatt.GetPrimaryServiceAsync(new Guid("000000ff-0000-1000-8000-00805f9b34fb"));
var characteristic = await service.GetCharacteristicAsync(new Guid("0000ff01-0000-1000-8000-00805f9b34fb"));

// Set opacity
await characteristic.WriteValueWithResponseAsync(new byte[] { 128 });
```

---

## Best Practices

### Update Rate
- **Maximum:** 20 Hz (50ms between commands)
- **Recommended:** 10 Hz for smooth visual feedback
- **Minimum for research:** 1 Hz

### Latency Considerations
- BLE write latency: ~10-50ms
- Total loop latency: ~50-100ms typical
- For tight timing, use LSL timestamps

### Power Management
- Device auto-sleeps after session
- Use `glasses.sleep()` when done
- Battery life: ~8 hours active use

### Error Handling
```python
from edge_glasses import Glasses, ConnectionError, DeviceNotFoundError

try:
    glasses = Glasses()
    await glasses.connect()
except DeviceNotFoundError:
    print("Glasses not found - is it powered on?")
except ConnectionError:
    print("Connection failed - try again")
```

---

## Support

- **GitHub Issues:** https://github.com/edge-glasses/python-sdk/issues
- **Documentation:** https://github.com/edge-glasses/docs
- **Community:** OpenBCI Discord, Muse Community

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | Feb 2026 | Initial release |
