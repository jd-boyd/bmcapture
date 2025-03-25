"""
Example showing how to list Blackmagic devices and check active input signals.
"""

import bmcapture
import sys
import time

def check_input_status(device_index, port_index, port_name):
    """
    Check if an input port has an active signal and return its format if available.
    Returns: (has_signal, width, height, fps) or (False, 0, 0, 0) if no signal
    """
    # Create a device instance
    try:
        device = bmcapture.create_device(device_index)
        if not device:
            return False, 0, 0, 0
        
        # Select the input port
        if not bmcapture.select_input_port(device, port_index):
            bmcapture.destroy_device(device)
            return False, 0, 0, 0
        
        # Common formats to try
        formats = [
            (1920, 1080, 30.0),
            (1920, 1080, 29.97),
            (1920, 1080, 60.0),
            (1920, 1080, 59.94),
            (1920, 1080, 50.0),
            (1920, 1080, 25.0),
            (1920, 1080, 24.0),
            (1920, 1080, 23.98),
            (1280, 720, 60.0),
            (1280, 720, 59.94),
            (1280, 720, 50.0),
            (720, 576, 50.0),    # PAL
            (720, 480, 59.94),   # NTSC
        ]
        
        # Try each format
        for width, height, fps in formats:
            try:
                # Create a capture instance
                cap = bmcapture.BMCapture(device_index, width, height, fps, True)
                
                # Wait for a frame
                for _ in range(3):  # Try a few times
                    if cap.update():
                        # Get the frame
                        frame = cap.get_frame(format='rgb')
                        if frame is not None:
                            actual_width = frame.shape[1]
                            actual_height = frame.shape[0]
                            cap.close()
                            return True, actual_width, actual_height, fps
                    time.sleep(0.1)
                
                cap.close()
            except Exception:
                # Just continue to next format if this one fails
                pass
        
        # Cleanup
        bmcapture.destroy_device(device)
        return False, 0, 0, 0
    except Exception as e:
        print(f"  Error checking input status: {e}")
        return False, 0, 0, 0

def main():
    # Get available devices
    devices = bmcapture.get_devices()
    print(f"Found {len(devices)} Blackmagic device(s)")
    
    if not devices:
        print("No devices found")
        return 1
    
    # List devices
    for device_index, device_name in enumerate(devices):
        print(f"Device {device_index}: {device_name}")
        
        # Get input ports for this device
        try:
            ports = bmcapture.get_input_ports(device_index)
            print(f"  Found {len(ports)} input port(s)")
            
            # Check each input port
            for port_index, port_name in enumerate(ports):
                print(f"  Port {port_index}: {port_name}")
                
                # Check if this port has an active signal
                has_signal, width, height, fps = check_input_status(device_index, port_index, port_name)
                
                if has_signal:
                    print(f"    Signal detected: {width}x{height} @ {fps:.2f} fps")
                else:
                    print(f"    No active signal detected")
        except Exception as e:
            print(f"  Error listing input ports: {e}")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())