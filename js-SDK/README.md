# EDGE Glasses JavaScript SDK

Control EDGE Smart LCD Glasses over Web Bluetooth.

## Installation

```bash
npm install edge-glasses
```

## Quick Start

```typescript
import { Glasses } from 'edge-glasses';

const glasses = new Glasses();

// Must be called from user gesture (button click)
document.getElementById('connect')?.addEventListener('click', async () => {
  await glasses.connect();
  await glasses.setOpacity(128);  // 50% dark
});
```

## Browser Support

Web Bluetooth is supported in:
- Chrome 56+ (desktop & Android)
- Edge 79+
- Opera 43+
- Samsung Internet 6.2+

**Not supported:** Firefox, Safari, iOS browsers

## Usage

### Basic Control

```typescript
import { Glasses } from 'edge-glasses';

const glasses = new Glasses();
await glasses.connect();

// Simple opacity control
await glasses.clear();           // Fully transparent
await glasses.setOpacity(128);   // 50% dark
await glasses.dark();            // Fully opaque

// Static hold
await glasses.hold(75);          // Hold at 75%

// Sleep
await glasses.sleep();

// Disconnect
glasses.disconnect();
```

### Timed Sessions

```typescript
// Start a custom session
await glasses.startSession({
  duration: 10,        // 10 minutes
  strobeStart: 12,     // Start at 12 Hz
  strobeEnd: 8,        // End at 8 Hz
  inhale: 4.0,         // 4s inhale
  holdInEnd: 4.0,      // Hold grows to 4s
  exhale: 4.0,         // 4s exhale
  holdOutEnd: 4.0,     // Hold grows to 4s
  brightness: 100      // Full brightness
});

// Or use presets
await glasses.sessionRelax(15);    // 15-min relaxation
await glasses.sessionFocus(10);    // 10-min focus
await glasses.sessionMeditate(10); // 10-min meditation
await glasses.sessionSleep(20);    // 20-min sleep prep
```

### Real-time Control

```typescript
// Update opacity in real-time (e.g., from sensor data)
function updateFromSensor(value: number) {
  // value is 0-1 from your sensor
  const opacity = Math.floor(value * 255);
  glasses.setOpacity(opacity);
}

// Example: 20 Hz update loop
setInterval(() => {
  const sensorValue = getSensorReading();  // Your function
  updateFromSensor(sensorValue);
}, 50);
```

## React Example

```tsx
import { useState, useCallback } from 'react';
import { Glasses } from 'edge-glasses';

const glasses = new Glasses();

function GlassesControl() {
  const [connected, setConnected] = useState(false);
  const [opacity, setOpacity] = useState(0);

  const handleConnect = useCallback(async () => {
    try {
      await glasses.connect();
      setConnected(true);
    } catch (err) {
      console.error('Connection failed:', err);
    }
  }, []);

  const handleOpacityChange = useCallback(async (e: React.ChangeEvent<HTMLInputElement>) => {
    const value = parseInt(e.target.value);
    setOpacity(value);
    if (connected) {
      await glasses.setOpacity(value);
    }
  }, [connected]);

  return (
    <div>
      {!connected ? (
        <button onClick={handleConnect}>Connect Glasses</button>
      ) : (
        <div>
          <input
            type="range"
            min="0"
            max="255"
            value={opacity}
            onChange={handleOpacityChange}
          />
          <span>{Math.round(opacity / 255 * 100)}%</span>
        </div>
      )}
    </div>
  );
}
```

## API Reference

### Connection

| Method | Description |
|--------|-------------|
| `connect()` | Connect to glasses (requires user gesture) |
| `disconnect()` | Disconnect from glasses |
| `isConnected` | Check connection status |
| `deviceName` | Get connected device name |

### Simple Control

| Method | Description |
|--------|-------------|
| `setOpacity(0-255)` | Set lens opacity |
| `clear()` | Fully transparent |
| `dark()` | Fully opaque |
| `hold(0-100)` | Hold at duty cycle % |
| `sleep()` | Enter deep sleep |

### Session Control

| Method | Description |
|--------|-------------|
| `startSession(config)` | Start custom session |
| `setStrobe(start, end)` | Set frequency range (Hz) |
| `setBreathing(inh, holdIn, exh, holdOut)` | Set breathing pattern |
| `setDuration(minutes)` | Set session length |
| `setBrightness(0-100)` | Set max brightness |
| `resume()` | Restart session |

### Preset Sessions

| Method | Description |
|--------|-------------|
| `sessionRelax(duration)` | Relaxation preset |
| `sessionFocus(duration)` | Focus/concentration preset |
| `sessionMeditate(duration)` | Meditation preset |
| `sessionSleep(duration)` | Sleep preparation preset |

## Integration Examples

### OpenBCI / brainflow

See the Python SDK for brainflow integration. For web apps, use a WebSocket bridge:

```typescript
const ws = new WebSocket('ws://localhost:8765');

ws.onmessage = async (event) => {
  const { alpha } = JSON.parse(event.data);
  await glasses.setOpacity(Math.floor(alpha * 255));
};
```

### Muse (via muse-js)

```typescript
import { MuseClient } from 'muse-js';
import { Glasses } from 'edge-glasses';

const muse = new MuseClient();
const glasses = new Glasses();

await muse.connect();
await glasses.connect();

muse.eegReadings.subscribe(reading => {
  // Process EEG and update glasses
  const alpha = calculateAlphaPower(reading);
  glasses.setOpacity(Math.floor(alpha * 255));
});
```

## TypeScript

Full TypeScript support with type definitions included.

```typescript
import { Glasses, SessionConfig, ScanResult } from 'edge-glasses';

const config: SessionConfig = {
  duration: 10,
  strobeStart: 12,
  strobeEnd: 8
};

await glasses.startSession(config);
```

## License

MIT License - see LICENSE file.
