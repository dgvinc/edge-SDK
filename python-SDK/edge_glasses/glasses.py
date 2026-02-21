"""
EDGE Glasses - Main SDK module
"""

import asyncio
from dataclasses import dataclass
from typing import Optional, List, Callable
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

from .exceptions import (
    ConnectionError,
    DeviceNotFoundError,
    CommandError,
    TimeoutError
)


# BLE UUIDs
SERVICE_UUID = "000000ff-0000-1000-8000-00805f9b34fb"
CHAR_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"
DEVICE_NAME = "Smart_Glasses"


@dataclass
class ScanResult:
    """Represents a discovered EDGE Glasses device"""
    name: str
    address: str
    rssi: int
    
    def __str__(self):
        return f"{self.name} ({self.address}) RSSI: {self.rssi}"


class Glasses:
    """
    EDGE Smart Glasses controller
    
    Usage:
        async with Glasses() as glasses:
            await glasses.set_opacity(128)  # 50% dark
    
    Or manually:
        glasses = Glasses()
        await glasses.connect()
        await glasses.set_opacity(128)
        await glasses.disconnect()
    """
    
    def __init__(self, address: Optional[str] = None):
        """
        Initialize glasses controller
        
        Args:
            address: Optional BLE address. If None, will scan for device.
        """
        self._address = address
        self._client: Optional[BleakClient] = None
        self._connected = False
        
    @property
    def is_connected(self) -> bool:
        """Check if currently connected"""
        return self._connected and self._client is not None
    
    @property
    def address(self) -> Optional[str]:
        """Get the device address"""
        return self._address
    
    # -------------------------------------------------------------------------
    # Connection Management
    # -------------------------------------------------------------------------
    
    @staticmethod
    async def scan(timeout: float = 5.0) -> List[ScanResult]:
        """
        Scan for EDGE Glasses devices
        
        Args:
            timeout: Scan duration in seconds
            
        Returns:
            List of discovered devices
        """
        devices = []
        
        discovered = await BleakScanner.discover(timeout=timeout)
        for d in discovered:
            if d.name and DEVICE_NAME in d.name:
                devices.append(ScanResult(
                    name=d.name,
                    address=d.address,
                    rssi=d.rssi or -100
                ))
        
        return sorted(devices, key=lambda x: x.rssi, reverse=True)
    
    async def connect(self, timeout: float = 10.0) -> None:
        """
        Connect to glasses
        
        Args:
            timeout: Connection timeout in seconds
            
        Raises:
            DeviceNotFoundError: If no device found during scan
            ConnectionError: If connection fails
        """
        # Find device if no address specified
        if not self._address:
            devices = await self.scan(timeout=5.0)
            if not devices:
                raise DeviceNotFoundError("No EDGE Glasses found. Is the device powered on?")
            self._address = devices[0].address
        
        # Connect
        try:
            self._client = BleakClient(self._address, timeout=timeout)
            await self._client.connect()
            self._connected = True
        except BleakError as e:
            raise ConnectionError(f"Failed to connect: {e}")
        except asyncio.TimeoutError:
            raise TimeoutError(f"Connection timed out after {timeout}s")
    
    async def disconnect(self) -> None:
        """Disconnect from glasses"""
        if self._client:
            try:
                await self._client.disconnect()
            except BleakError:
                pass  # Ignore disconnect errors
            finally:
                self._connected = False
                self._client = None
    
    async def __aenter__(self):
        """Async context manager entry"""
        await self.connect()
        return self
    
    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Async context manager exit"""
        await self.disconnect()
        return False
    
    # -------------------------------------------------------------------------
    # Low-level Commands
    # -------------------------------------------------------------------------
    
    async def _send(self, data: bytes) -> None:
        """
        Send raw bytes to glasses
        
        Args:
            data: Bytes to send
            
        Raises:
            ConnectionError: If not connected
            CommandError: If write fails
        """
        if not self.is_connected:
            raise ConnectionError("Not connected. Call connect() first.")
        
        try:
            await self._client.write_gatt_char(CHAR_UUID, data, response=True)
        except BleakError as e:
            raise CommandError(f"Command failed: {e}")
    
    # -------------------------------------------------------------------------
    # Simple Control (Legacy API)
    # -------------------------------------------------------------------------
    
    async def set_opacity(self, value: int) -> None:
        """
        Set lens opacity (simple mode)
        
        This is the basic control mode - sets static opacity and stops
        any running session.
        
        Args:
            value: Opacity 0-255 (0=clear, 255=full dark)
            
        Example:
            await glasses.set_opacity(0)    # Clear
            await glasses.set_opacity(128)  # 50% dark
            await glasses.set_opacity(255)  # Full dark
        """
        value = max(0, min(255, int(value)))
        await self._send(bytes([value]))
    
    async def clear(self) -> None:
        """Set lenses to fully clear (transparent)"""
        await self.set_opacity(0)
    
    async def dark(self) -> None:
        """Set lenses to fully dark (opaque)"""
        await self.set_opacity(255)
    
    # -------------------------------------------------------------------------
    # Extended Control (v4.0 API)
    # -------------------------------------------------------------------------
    
    async def set_strobe(self, start_hz: int, end_hz: int) -> None:
        """
        Set strobe frequency range for session
        
        Frequency progresses linearly from start to end over session duration.
        Restarts the current session.
        
        Args:
            start_hz: Starting frequency 1-50 Hz
            end_hz: Ending frequency 1-50 Hz
            
        Example:
            await glasses.set_strobe(12, 8)  # 12Hz -> 8Hz over session
        """
        start_hz = max(1, min(50, int(start_hz)))
        end_hz = max(1, min(50, int(end_hz)))
        await self._send(bytes([0xA1, start_hz, end_hz]))
    
    async def set_brightness(self, percent: int) -> None:
        """
        Set maximum brightness level
        
        Does not restart session.
        
        Args:
            percent: Brightness 0-100%
        """
        percent = max(0, min(100, int(percent)))
        await self._send(bytes([0xA2, percent]))
    
    async def set_breathing(
        self,
        inhale: float,
        hold_in_end: float,
        exhale: float,
        hold_out_end: float
    ) -> None:
        """
        Set breathing pattern parameters
        
        The breathing pattern has 4 phases:
        1. Inhale: lenses go clear -> dark
        2. Hold in: lenses stay dark (duration grows from 0 to hold_in_end)
        3. Exhale: lenses go dark -> clear
        4. Hold out: lenses stay clear (duration grows from 0 to hold_out_end)
        
        Restarts the current session.
        
        Args:
            inhale: Inhale duration in seconds (fixed)
            hold_in_end: Final hold-in duration in seconds (starts at 0)
            exhale: Exhale duration in seconds (fixed)
            hold_out_end: Final hold-out duration in seconds (starts at 0)
            
        Example:
            # 4s inhale, 0->4s hold, 4s exhale, 0->4s hold
            await glasses.set_breathing(4.0, 4.0, 4.0, 4.0)
        """
        # Convert seconds to 0.1s units (max 25.5s per phase)
        inh = max(0, min(255, int(inhale * 10)))
        h_in = max(0, min(255, int(hold_in_end * 10)))
        exh = max(0, min(255, int(exhale * 10)))
        h_out = max(0, min(255, int(hold_out_end * 10)))
        await self._send(bytes([0xA3, inh, h_in, exh, h_out]))
    
    async def set_duration(self, minutes: int) -> None:
        """
        Set session duration
        
        Restarts the current session.
        
        Args:
            minutes: Session length 1-60 minutes
        """
        minutes = max(1, min(60, int(minutes)))
        await self._send(bytes([0xA4, minutes]))
    
    async def hold(self, duty: int) -> None:
        """
        Hold at static duty cycle (stops session)
        
        Use this for manual control without the breathing/strobe program.
        
        Args:
            duty: Duty cycle 0-100%
        """
        duty = max(0, min(100, int(duty)))
        await self._send(bytes([0xA5, duty]))
    
    async def resume(self) -> None:
        """Resume/restart the timed session"""
        await self._send(bytes([0xA6]))
    
    async def sleep(self) -> None:
        """Put glasses into deep sleep mode"""
        await self._send(bytes([0xA7]))
    
    # -------------------------------------------------------------------------
    # High-level Session Control
    # -------------------------------------------------------------------------
    
    async def start_session(
        self,
        duration: int = 10,
        strobe_start: int = 12,
        strobe_end: int = 8,
        inhale: float = 4.0,
        hold_in_end: float = 4.0,
        exhale: float = 4.0,
        hold_out_end: float = 4.0,
        brightness: int = 100
    ) -> None:
        """
        Configure and start a complete meditation session
        
        This is the recommended high-level method for starting sessions.
        Sets all parameters and starts the session.
        
        Args:
            duration: Session length in minutes (1-60)
            strobe_start: Starting strobe frequency Hz (1-50)
            strobe_end: Ending strobe frequency Hz (1-50)
            inhale: Inhale duration seconds
            hold_in_end: Final hold-in duration seconds
            exhale: Exhale duration seconds
            hold_out_end: Final hold-out duration seconds
            brightness: Maximum brightness 0-100%
            
        Example:
            # 20-minute relaxation session
            await glasses.start_session(
                duration=20,
                strobe_start=10,
                strobe_end=4,
                inhale=5.0,
                hold_in_end=5.0,
                exhale=5.0,
                hold_out_end=5.0,
                brightness=100
            )
        """
        await self.set_brightness(brightness)
        await self.set_breathing(inhale, hold_in_end, exhale, hold_out_end)
        await self.set_strobe(strobe_start, strobe_end)
        await self.set_duration(duration)  # This restarts the session
    
    # -------------------------------------------------------------------------
    # Preset Sessions
    # -------------------------------------------------------------------------
    
    async def session_relax(self, duration: int = 10) -> None:
        """
        Start a relaxation session
        
        Slower strobe (10->4 Hz), longer breathing cycles.
        Good for winding down, stress relief, pre-sleep.
        
        Args:
            duration: Session length in minutes
        """
        await self.start_session(
            duration=duration,
            strobe_start=10,
            strobe_end=4,
            inhale=5.0,
            hold_in_end=5.0,
            exhale=5.0,
            hold_out_end=5.0
        )
    
    async def session_focus(self, duration: int = 10) -> None:
        """
        Start a focus/concentration session
        
        Higher strobe frequencies (15->10 Hz), shorter breathing.
        Good for studying, working, alertness.
        
        Args:
            duration: Session length in minutes
        """
        await self.start_session(
            duration=duration,
            strobe_start=15,
            strobe_end=10,
            inhale=3.0,
            hold_in_end=2.0,
            exhale=3.0,
            hold_out_end=2.0
        )
    
    async def session_meditate(self, duration: int = 10) -> None:
        """
        Start a meditation session
        
        Default balanced settings (12->8 Hz), standard breathing.
        Good for general meditation practice.
        
        Args:
            duration: Session length in minutes
        """
        await self.start_session(
            duration=duration,
            strobe_start=12,
            strobe_end=8,
            inhale=4.0,
            hold_in_end=4.0,
            exhale=4.0,
            hold_out_end=4.0
        )
    
    async def session_sleep(self, duration: int = 15) -> None:
        """
        Start a sleep preparation session
        
        Very slow strobe (6->2 Hz), long slow breathing.
        Device will auto-sleep when session ends.
        
        Args:
            duration: Session length in minutes
        """
        await self.start_session(
            duration=duration,
            strobe_start=6,
            strobe_end=2,
            inhale=6.0,
            hold_in_end=6.0,
            exhale=6.0,
            hold_out_end=6.0
        )
