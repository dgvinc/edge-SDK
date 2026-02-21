"""
EDGE Glasses SDK - Exceptions
"""


class GlassesError(Exception):
    """Base exception for EDGE Glasses SDK"""
    pass


class ConnectionError(GlassesError):
    """Failed to connect to device"""
    pass


class DeviceNotFoundError(GlassesError):
    """No EDGE Glasses device found"""
    pass


class CommandError(GlassesError):
    """Failed to send command to device"""
    pass


class TimeoutError(GlassesError):
    """Operation timed out"""
    pass
