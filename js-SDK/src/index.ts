/**
 * EDGE Glasses JavaScript/TypeScript SDK
 * Control smart LCD glasses over Web Bluetooth
 * 
 * @module edge-glasses
 * @version 1.0.0
 */

// BLE UUIDs
const SERVICE_UUID = 0x00ff;
const CHAR_UUID = 0xff01;
const DEVICE_NAME = 'Smart_Glasses';

/**
 * Scan result from BLE discovery
 */
export interface ScanResult {
  device: BluetoothDevice;
  name: string;
}

/**
 * Session configuration options
 */
export interface SessionConfig {
  duration?: number;      // minutes (1-60)
  strobeStart?: number;   // Hz (1-50)
  strobeEnd?: number;     // Hz (1-50)
  inhale?: number;        // seconds
  holdInEnd?: number;     // seconds
  exhale?: number;        // seconds
  holdOutEnd?: number;    // seconds
  brightness?: number;    // percent (0-100)
}

/**
 * EDGE Smart Glasses Controller
 * 
 * @example
 * ```typescript
 * const glasses = new Glasses();
 * await glasses.connect();
 * await glasses.setOpacity(128);  // 50% dark
 * await glasses.startSession({ duration: 10 });
 * ```
 */
export class Glasses {
  private device: BluetoothDevice | null = null;
  private server: BluetoothRemoteGATTServer | null = null;
  private characteristic: BluetoothRemoteGATTCharacteristic | null = null;
  private _connected = false;

  /**
   * Check if currently connected
   */
  get isConnected(): boolean {
    return this._connected && this.server?.connected === true;
  }

  /**
   * Get device name
   */
  get deviceName(): string | undefined {
    return this.device?.name;
  }

  // -------------------------------------------------------------------------
  // Connection Management
  // -------------------------------------------------------------------------

  /**
   * Request and connect to EDGE Glasses
   * Uses Web Bluetooth API - must be called from user gesture
   * 
   * @throws Error if Bluetooth unavailable or connection fails
   */
  async connect(): Promise<void> {
    if (!navigator.bluetooth) {
      throw new Error('Web Bluetooth not supported. Use Chrome/Edge on desktop or Android.');
    }

    try {
      // Request device
      this.device = await navigator.bluetooth.requestDevice({
        filters: [{ namePrefix: DEVICE_NAME }],
        optionalServices: [SERVICE_UUID]
      });

      if (!this.device.gatt) {
        throw new Error('GATT not available on device');
      }

      // Connect to GATT server
      this.server = await this.device.gatt.connect();

      // Get service and characteristic
      const service = await this.server.getPrimaryService(SERVICE_UUID);
      this.characteristic = await service.getCharacteristic(CHAR_UUID);

      this._connected = true;

      // Handle disconnection
      this.device.addEventListener('gattserverdisconnected', () => {
        this._connected = false;
        console.log('EDGE Glasses disconnected');
      });

    } catch (error) {
      this._connected = false;
      throw new Error(`Connection failed: ${error}`);
    }
  }

  /**
   * Disconnect from glasses
   */
  disconnect(): void {
    if (this.server?.connected) {
      this.server.disconnect();
    }
    this._connected = false;
    this.device = null;
    this.server = null;
    this.characteristic = null;
  }

  // -------------------------------------------------------------------------
  // Low-level Commands
  // -------------------------------------------------------------------------

  /**
   * Send raw bytes to glasses
   */
  private async send(data: number[]): Promise<void> {
    if (!this.isConnected || !this.characteristic) {
      throw new Error('Not connected. Call connect() first.');
    }

    const buffer = new Uint8Array(data);
    await this.characteristic.writeValueWithResponse(buffer);
  }

  // -------------------------------------------------------------------------
  // Simple Control (Legacy API)
  // -------------------------------------------------------------------------

  /**
   * Set lens opacity (simple mode)
   * Stops any running session and holds static opacity
   * 
   * @param value Opacity 0-255 (0=clear, 255=full dark)
   */
  async setOpacity(value: number): Promise<void> {
    value = Math.max(0, Math.min(255, Math.floor(value)));
    await this.send([value]);
  }

  /**
   * Set lenses to fully clear (transparent)
   */
  async clear(): Promise<void> {
    await this.setOpacity(0);
  }

  /**
   * Set lenses to fully dark (opaque)
   */
  async dark(): Promise<void> {
    await this.setOpacity(255);
  }

  // -------------------------------------------------------------------------
  // Extended Control (v4.0 API)
  // -------------------------------------------------------------------------

  /**
   * Set strobe frequency range
   * Restarts the current session
   * 
   * @param startHz Starting frequency 1-50 Hz
   * @param endHz Ending frequency 1-50 Hz
   */
  async setStrobe(startHz: number, endHz: number): Promise<void> {
    startHz = Math.max(1, Math.min(50, Math.floor(startHz)));
    endHz = Math.max(1, Math.min(50, Math.floor(endHz)));
    await this.send([0xA1, startHz, endHz]);
  }

  /**
   * Set maximum brightness level
   * Does not restart session
   * 
   * @param percent Brightness 0-100%
   */
  async setBrightness(percent: number): Promise<void> {
    percent = Math.max(0, Math.min(100, Math.floor(percent)));
    await this.send([0xA2, percent]);
  }

  /**
   * Set breathing pattern parameters
   * Restarts the current session
   * 
   * @param inhale Inhale duration in seconds
   * @param holdInEnd Final hold-in duration in seconds (starts at 0)
   * @param exhale Exhale duration in seconds
   * @param holdOutEnd Final hold-out duration in seconds (starts at 0)
   */
  async setBreathing(
    inhale: number,
    holdInEnd: number,
    exhale: number,
    holdOutEnd: number
  ): Promise<void> {
    // Convert seconds to 0.1s units (max 25.5s)
    const inh = Math.max(0, Math.min(255, Math.floor(inhale * 10)));
    const hIn = Math.max(0, Math.min(255, Math.floor(holdInEnd * 10)));
    const exh = Math.max(0, Math.min(255, Math.floor(exhale * 10)));
    const hOut = Math.max(0, Math.min(255, Math.floor(holdOutEnd * 10)));
    await this.send([0xA3, inh, hIn, exh, hOut]);
  }

  /**
   * Set session duration
   * Restarts the current session
   * 
   * @param minutes Session length 1-60 minutes
   */
  async setDuration(minutes: number): Promise<void> {
    minutes = Math.max(1, Math.min(60, Math.floor(minutes)));
    await this.send([0xA4, minutes]);
  }

  /**
   * Hold at static duty cycle (stops session)
   * 
   * @param duty Duty cycle 0-100%
   */
  async hold(duty: number): Promise<void> {
    duty = Math.max(0, Math.min(100, Math.floor(duty)));
    await this.send([0xA5, duty]);
  }

  /**
   * Resume/restart the timed session
   */
  async resume(): Promise<void> {
    await this.send([0xA6]);
  }

  /**
   * Put glasses into deep sleep mode
   */
  async sleep(): Promise<void> {
    await this.send([0xA7]);
  }

  // -------------------------------------------------------------------------
  // High-level Session Control
  // -------------------------------------------------------------------------

  /**
   * Configure and start a complete session
   * 
   * @param config Session configuration
   */
  async startSession(config: SessionConfig = {}): Promise<void> {
    const {
      duration = 10,
      strobeStart = 12,
      strobeEnd = 8,
      inhale = 4.0,
      holdInEnd = 4.0,
      exhale = 4.0,
      holdOutEnd = 4.0,
      brightness = 100
    } = config;

    await this.setBrightness(brightness);
    await this.setBreathing(inhale, holdInEnd, exhale, holdOutEnd);
    await this.setStrobe(strobeStart, strobeEnd);
    await this.setDuration(duration);  // This restarts the session
  }

  // -------------------------------------------------------------------------
  // Preset Sessions
  // -------------------------------------------------------------------------

  /**
   * Start a relaxation session
   * Slower strobe, longer breathing cycles
   */
  async sessionRelax(duration = 10): Promise<void> {
    await this.startSession({
      duration,
      strobeStart: 10,
      strobeEnd: 4,
      inhale: 5.0,
      holdInEnd: 5.0,
      exhale: 5.0,
      holdOutEnd: 5.0
    });
  }

  /**
   * Start a focus/concentration session
   * Higher strobe, shorter breathing
   */
  async sessionFocus(duration = 10): Promise<void> {
    await this.startSession({
      duration,
      strobeStart: 15,
      strobeEnd: 10,
      inhale: 3.0,
      holdInEnd: 2.0,
      exhale: 3.0,
      holdOutEnd: 2.0
    });
  }

  /**
   * Start a meditation session
   * Balanced settings
   */
  async sessionMeditate(duration = 10): Promise<void> {
    await this.startSession({
      duration,
      strobeStart: 12,
      strobeEnd: 8,
      inhale: 4.0,
      holdInEnd: 4.0,
      exhale: 4.0,
      holdOutEnd: 4.0
    });
  }

  /**
   * Start a sleep preparation session
   * Very slow strobe, long slow breathing
   */
  async sessionSleep(duration = 15): Promise<void> {
    await this.startSession({
      duration,
      strobeStart: 6,
      strobeEnd: 2,
      inhale: 6.0,
      holdInEnd: 6.0,
      exhale: 6.0,
      holdOutEnd: 6.0
    });
  }
}

// Default export
export default Glasses;
