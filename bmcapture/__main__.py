"""
Main entry point for running the bmcapture module directly
"""
import sys
import bmcapture
from bmcapture.utils import list_devices

def main():
    """Print information about available BlackMagic devices"""
    print("bmcapture version", bmcapture.__version__)
    print("\nAvailable BlackMagic devices:")
    
    devices = list_devices()
    if not devices:
        print("No devices found.")
        return
    
    for idx, name in devices:
        print(f"  [{idx}] {name}")
        
        # Get input ports for this device
        ports = bmcapture.get_input_ports(idx)
        if ports:
            print("    Input ports:")
            for i, port in enumerate(ports):
                print(f"      [{i}] {port}")
        else:
            print("    No input ports found")
        
        print()
    
    print("\nUse the examples in the examples/ directory to capture video.")

if __name__ == "__main__":
    main()