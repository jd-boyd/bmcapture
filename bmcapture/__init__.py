"""
BlackMagic Capture Python API

This module provides a Pythonic wrapper around the C-based bmcapture_c module.
"""

import bmcapture_c

# Re-export all public classes, functions, and constants
from bmcapture_c import (
    # Constants
    LOW_LATENCY,
    NO_FRAME_DROPS,
    
    # Classes
    BMCapture, 
    BMChannel,
    
    # Functions 
    initialize,
    shutdown,
    get_device_count,
    get_device_name,
    get_devices,
    get_input_ports,
    create_device,
    select_input_port,
    destroy_device,
)

# Version information
__version__ = "0.1.0"