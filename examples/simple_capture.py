"""
Simple example showing how to use the bmcapture library.
This example captures frames from a Blackmagic device and saves them as PNG files.
"""

import bmcapture
import numpy as np
from PIL import Image
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
        
        print("Waiting for valid signal...")
        # Wait for a valid signal before starting capture
        signal_check_start = time.time()
        frame_check_count = 0
        min_good_frames = 3  # Minimum consecutive good frames to consider signal valid
        
        while True:
            # Update and check if we got a frame
            if cap.update():
                frame_check_count += 1
                print(f"Received frame {frame_check_count}, checking for stable signal...")
                
                # After receiving a few consecutive frames, consider the signal stable
                if frame_check_count >= min_good_frames:
                    print("Signal appears stable after receiving consecutive frames")
                    break
            
            time.sleep(0.1)  # Check every 100ms
            
            # Add a timeout to avoid infinite wait
            if time.time() - signal_check_start > 5.0:
                print("Warning: Timeout waiting for signal lock, proceeding anyway")
                break
        
        total_frames = cap.get_frame_count() if hasattr(cap, 'get_frame_count') else frame_check_count
        print(f"Signal locked! Starting capture with {total_frames} frames received.")
        
        start_time = time.time()
        old_frame_time = start_time
        
        print("Capturing frames... Press Ctrl+C to stop.")
        while frame_count < 100:  # Capture 100 frames
            sleep_cnt = 1
            # Wait for a new frame to become available
            new_frame = False
            while not new_frame:
                new_frame = cap.update()
                if not new_frame:
                    # Sleep a bit to avoid busy-waiting
                    time.sleep(0.01)  # 10ms sleep between checks
                    sleep_cnt = sleep_cnt + 1
            
            new_frame_time = time.time()
            old_frame_time = new_frame_time
            
            # Get frame as RGB numpy array
            frame = cap.get_frame(format='rgb')
            
            # Check if frame contains valid data
            # Detect black frames by checking average pixel value
            is_valid = True
            if frame is not None:
                avg_intensity = np.mean(frame)
                if avg_intensity < 1.0:  # Threshold for nearly black frame
                    is_valid = False
            else:
                is_valid = False
            
            # For invalid frames, let's try again after a brief wait
            if not is_valid and frame is not None:
                
                # First make sure we tell the library we want the next frame
                cap.update()
                
                # Small sleep to ensure the frame buffer is updated properly
                time.sleep(0.03)  # 30ms is usually enough for a buffer swap
                
                # Get the frame again
                frame = cap.get_frame(format='rgb')
                
                # Check the new frame
                if frame is not None:
                    avg_intensity = np.mean(frame)
                    
                    # If still black, try one more time with a different strategy
                    if avg_intensity < 1.0:
                        # Force multiple updates to try to get past any buffer sync issues
                        for _ in range(3):
                            cap.update()
                            time.sleep(0.01)
                        frame = cap.get_frame(format='rgb')
            
            # Convert to PIL Image and save
            if frame is not None:
                img = Image.fromarray(frame)
                
                # Make sure output directory exists
                os.makedirs("frame_dump", exist_ok=True)
                img.save(f"frame_dump/frame_{frame_count:04d}.jpg")

            frame_count += 1
            if frame_count % 10 == 0:
                frame_count_text = f"Captured {frame_count} frames"
                if hasattr(cap, 'get_frame_count'):
                    frame_count_text += f", frame counter: {cap.get_frame_count()}"
                print(frame_count_text)
            old_frame_time = new_frame_time

        stop_time = time.time()
        elapsed = stop_time - start_time
        print(f"Start: {start_time} Stop: {stop_time}")
        print(f"Captured {frame_count} frames in {elapsed:.2f} seconds")
        print(f"Average FPS: {frame_count / elapsed:.2f}")

    except KeyboardInterrupt:
        print(f"\nCapture stopped after {frame_count} frames")
    finally:
        # Clean up
        print("Closing capture device...")
        cap.close()

if __name__ == "__main__":
    main()
