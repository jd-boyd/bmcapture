"""
Simple example showing how to use the bmcapture library.
This example captures frames from a Blackmagic device and saves them as PNG files.
"""

import bmcapture
from bmcapture import utils as bm_utils
import numpy as np
import cv2
import time
import os

def main():
    # List available devices
    devices = bmcapture.get_devices()
    print(f"Available devices: {devices}")

    if not devices:
        print("No Blackmagic devices found.")
        return

    # Create output directory if it doesn't exist
    os.makedirs("frame_dump", exist_ok=True)

    # Initialize capture device
    # device_index=0, width=1920, height=1080, framerate=30.0, low_latency=True
    print("Initializing capture device...")

    # Try different common frame rates
    success = False
    for framerate in [24.0, 29.97, 30.0, 23.98, 25.0, 59.94, 60.0]:
        try:
            print(f"Trying to initialize with 1920x1080 @ {framerate} fps...")
            cap = bmcapture.BMCapture(0, 1920, 1080, framerate, True)
            print(f"Success with framerate {framerate}!")
            success = True
            break
        except Exception as e:
            print(f"Failed with framerate {framerate}: {e}")

    # If all 1080p modes failed, try a 720p mode
    if not success:
        try:
            print("Trying to initialize with 1280x720 @ 59.94 fps...")
            cap = bmcapture.BMCapture(0, 1280, 720, 59.94, True)
            print("Success with 1280x720 @ 59.94 fps!")
            success = True
        except Exception as e:
            print(f"Failed with 720p: {e}")

    if not success:
        print("Could not initialize any supported mode")
        return

    try:
        frame_count = 0

        print("Capturing frames... Press Ctrl+C to stop.")
        start_time = time.time()
        while frame_count < 100:  # Capture 100 frames
            # Check for new frame
            if cap.update():
                # Get frame as RGB numpy array
                frame = cap.get_frame(format='rgb')

                # Convert from RGB to BGR for OpenCV and save
                frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                cv2.imwrite(f"frame_dump/frame_{frame_count:04d}.png", frame_bgr)

                frame_count += 1
                if frame_count % 10 == 0:
                    print(f"Captured {frame_count} frames")

            # Small sleep to prevent CPU spinning
            time.sleep(0.001)


        stop_time = time.time()
        elapsed = stop_time - start_time
        print(f"Start: {start_time} Stop: {stop_time}")
        print(f"Captured {frame_count} frames in {elapsed} seconds")
        print(f"Average FPS: {frame_count / elapsed:.2f}")

    except KeyboardInterrupt:
        print(f"\nCapture stopped after {frame_count} frames")
    finally:
        # Clean up
        print("Closing capture device...")
        cap.close()

if __name__ == "__main__":
    main()
