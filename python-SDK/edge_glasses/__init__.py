"""
EDGE Glasses Python SDK
Control smart LCD glasses over Bluetooth Low Energy
"""

from .glasses import Glasses, ScanResult
from .exceptions import (
    GlassesError,
    ConnectionError,
    DeviceNotFoundError,
    CommandError,
    TimeoutError
)

__version__ = "1.0.0"
__all__ = [
    "Glasses",
    "ScanResult", 
    "GlassesError",
    "ConnectionError",
    "DeviceNotFoundError",
    "CommandError",
    "TimeoutError"
]
