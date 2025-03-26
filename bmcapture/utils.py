"""
Utility functions for the bmcapture package
"""
import bmcapture_c
import numpy as np
from typing import List, Tuple, Optional


def list_devices() -> List[Tuple[int, str]]:
    """
    Returns a list of available Blackmagic devices with their indices and names.

    Returns:
        List of (index, name) tuples for all available devices
    """
    count = bmcapture_c.get_device_count()
    devices = []

    for i in range(count):
        name = bmcapture_c.get_device_name(i)
        devices.append((i, name))

    return devices


def wait_for_signal(cap, timeout: float = 5.0, min_good_frames: int = 3) -> bool:
    """
    Wait for a valid signal from the capture device.

    Args:
        cap: A BMCapture instance
        timeout: Maximum time to wait in seconds
        min_good_frames: Minimum consecutive good frames to consider signal valid

    Returns:
        True if signal is valid, False if timeout occurred
    """
    import time

    frame_check_count = 0
    signal_check_start = time.time()

    while True:
        # Update and check if we got a frame
        if cap.update():
            frame_check_count += 1

            # After receiving a few consecutive frames, consider the signal stable
            if frame_check_count >= min_good_frames:
                return True

        # Check for timeout
        if time.time() - signal_check_start > timeout:
            return False

        time.sleep(0.1)  # Check every 100ms
