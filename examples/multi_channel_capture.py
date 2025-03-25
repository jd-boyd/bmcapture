#!/usr/bin/env python3
"""
Advanced example showing how to use multiple channels on a Blackmagic device.
This example uses the new signal locking and buffer priming features to quickly
display video from multiple inputs simultaneously.
"""

import bmcapture
import numpy as np
import cv2
import time
import argparse

def main():
    # Parse command line arguments
    parser = argparse.ArgumentParser(description='Multi-channel Blackmagic capture.')
    parser.add_argument('--device', type=int, default=0, help='Device index (default: 0)')
    parser.add_argument('--width', type=int, default=1920, help='Capture width (default: 1920)')
    parser.add_argument('--height', type=int, default=1080, help='Capture height (default: 1080)')
    parser.add_argument('--fps', type=float, default=30.0, help='Capture framerate (default: 30.0)')
    args = parser.parse_args()

    # List available devices
    devices = bmcapture.get_devices()
    print(f"Available devices: {devices}")

    if not devices:
        print("No Blackmagic devices found.")
        return

    # Initialize primary device/channel
    print(f"Initializing device {args.device}: {devices[args.device]}")
    try:
        # Initialize with first port
        cap = bmcapture.BMCapture(args.device, args.width, args.height, args.fps, True, port_index=0)
        print("Primary channel initialized on port 0")
    except Exception as e:
        print(f"Failed to initialize primary channel: {e}")
        return

    # Get the number of channels supported by this device
    channel_count = cap.get_channel_count()
    print(f"Device supports {channel_count} channels")

    channels = []
    # Create additional channels if supported
    if channel_count > 1:
        for port in range(1, min(channel_count, 4)):  # Limit to 4 channels for display purposes
            try:
                channel = cap.create_channel(port_index=port, width=args.width, height=args.height, framerate=args.fps)
                channels.append(channel)
                print(f"Additional channel initialized on port {port}")
            except Exception as e:
                print(f"Failed to initialize channel on port {port}: {e}")

    # Wait for signal lock on all channels
    print("Waiting for signal lock on all channels...")
    timeout = 5.0  # 5 second timeout
    start_time = time.time()
    
    while time.time() - start_time < timeout:
        # Check primary channel
        primary_locked = cap.has_valid_signal()
        print(f"Primary channel: {'LOCKED' if primary_locked else 'NO SIGNAL'}, frames: {cap.get_frame_count()}")
        
        # Check additional channels
        all_locked = primary_locked
        for i, channel in enumerate(channels):
            channel_locked = channel.has_valid_signal()
            print(f"Channel {i+1}: {'LOCKED' if channel_locked else 'NO SIGNAL'}, frames: {channel.get_frame_count()}")
            all_locked = all_locked and channel_locked
        
        if all_locked and len(channels) > 0:
            print("All channels locked!")
            break
            
        # If we have at least the primary channel, we can proceed
        if primary_locked and time.time() - start_time > 2.0:
            print("Primary channel locked, proceeding...")
            break
            
        time.sleep(0.5)  # Check every 500ms
    
    # Create window for display
    if channels:
        # Multi-channel display
        cv2.namedWindow("Multi-Channel Capture", cv2.WINDOW_NORMAL)
        # Make the window an appropriate size for multiple cameras
        cv2.resizeWindow("Multi-Channel Capture", 1280, 720)
    else:
        # Single channel display
        cv2.namedWindow("Blackmagic Capture", cv2.WINDOW_NORMAL)
        cv2.resizeWindow("Blackmagic Capture", 1280, 720)

    # Start capture loop
    try:
        frame_count = 0
        start_time = time.time()
        
        while True:
            # Update all channels
            primary_updated = cap.update()
            
            channel_updates = []
            for channel in channels:
                channel_updates.append(channel.update())
            
            # Get frames from all updated channels
            frames = []
            
            if primary_updated:
                try:
                    primary_frame = cap.get_frame(format='rgb')
                    primary_frame = cv2.cvtColor(primary_frame, cv2.COLOR_RGB2BGR)
                    
                    # Add overlay 
                    cv2.putText(primary_frame, "Channel 0", (10, 30), 
                                cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
                    fps_text = f"FPS: {frame_count / (time.time() - start_time):.1f}"
                    cv2.putText(primary_frame, fps_text, (10, 70), 
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                    
                    frames.append(primary_frame)
                    frame_count += 1
                except Exception as e:
                    print(f"Error getting frame from primary channel: {e}")
            
            # Get frames from additional channels
            for i, (channel, updated) in enumerate(zip(channels, channel_updates)):
                if updated:
                    try:
                        channel_frame = channel.get_frame(format='rgb')
                        channel_frame = cv2.cvtColor(channel_frame, cv2.COLOR_RGB2BGR)
                        
                        # Add overlay
                        cv2.putText(channel_frame, f"Channel {i+1}", (10, 30), 
                                    cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
                        
                        frames.append(channel_frame)
                    except Exception as e:
                        print(f"Error getting frame from channel {i+1}: {e}")
            
            # Display frames
            if frames:
                if len(frames) == 1:
                    # Single channel display
                    cv2.imshow("Blackmagic Capture", frames[0])
                else:
                    # Multi-channel display in a grid
                    rows = int(np.ceil(len(frames) / 2))
                    cols = min(len(frames), 2)
                    
                    # Resize frames to fit in grid
                    resized_frames = []
                    for frame in frames:
                        # Resize to half size for grid display
                        resized = cv2.resize(frame, (frame.shape[1] // 2, frame.shape[0] // 2))
                        resized_frames.append(resized)
                    
                    # Create grid
                    if len(resized_frames) <= 2:
                        # Horizontal layout for 2 cameras
                        grid = np.hstack(resized_frames)
                    else:
                        # Create rows
                        rows_list = []
                        for row in range(rows):
                            row_frames = resized_frames[row*cols:min((row+1)*cols, len(resized_frames))]
                            # Pad the last row if needed
                            if len(row_frames) < cols:
                                h, w, c = row_frames[0].shape
                                padding = np.zeros((h, w * (cols - len(row_frames)), c), dtype=np.uint8)
                                row_frames.append(padding)
                            rows_list.append(np.hstack(row_frames))
                        
                        # Stack rows vertically
                        grid = np.vstack(rows_list)
                    
                    cv2.imshow("Multi-Channel Capture", grid)
                
                # Print FPS every 30 frames
                if frame_count % 30 == 0:
                    fps = frame_count / (time.time() - start_time)
                    print(f"FPS: {fps:.2f}")
            
            # Check for exit
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

    except KeyboardInterrupt:
        print("Capture interrupted")

    finally:
        # Clean up
        for channel in channels:
            channel.close()
        cap.close()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    main()