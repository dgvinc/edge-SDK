"""
EDGE Glasses Python SDK
Control smart LCD glasses over Bluetooth Low Energy
"""

from .glasses import (
    Glasses,
    ScanResult,
    Waveform,
    FeedbackStream,
    StandaloneMode,
    StandaloneProgram,
    StandaloneConfig,
    STANDALONE_MAX_PROGRAMS,
)
from .exceptions import (
    GlassesError,
    ConnectionError,
    DeviceNotFoundError,
    CommandError,
    TimeoutError
)

__version__ = "2.5.0"
__all__ = [
    "Glasses",
    "FeedbackStream",
    "ScanResult",
    "Waveform",
    "StandaloneMode",
    "StandaloneProgram",
    "StandaloneConfig",
    "STANDALONE_MAX_PROGRAMS",
    "GlassesError",
    "ConnectionError",
    "DeviceNotFoundError",
    "CommandError",
    "TimeoutError"
]
