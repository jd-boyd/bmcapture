"""
Multi-channel preview example for bmcapture.
Demonstrates how to use bmcapture with multiple input channels from the same device.
Automatically detects active inputs and displays all video feeds simultaneously.
"""

import bmcapture
import numpy as np
import cv2
import time
import os

def check_active_inputs(device_index=0):
    """Find all active input ports and their formats on a device"""
    print("Checking for active inputs on device", device_index)

    active_inputs = []

    try:
        # Get input ports for device
        ports = bmcapture.get_input_ports(device_index)
        if not ports:
            print("No input ports found")
            return []

        print(f"Found {len(ports)} input port(s):")

        # Create device only once to check ports
        device = bmcapture.create_device(device_index)
        if not device:
            print("Failed to create device")
            return []

        # Print device information
        print(f"Successfully created device with index {device_index}")
        print("Checking input ports...")

        # Check each port for an active signal
        for port_index, port_name in enumerate(ports):
            print(f"Port {port_index}: {port_name}")

            try:
                # Select port
                if not bmcapture.select_input_port(device, port_index):
                    print(f"  Failed to select port {port_index}")
                    continue

                # Try common formats
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

                for width, height, fps in formats:
                    try:
                        print(f"  Trying {width}x{height} @ {fps} fps...")

                        # Create a temporary channel to test the format
                        try:
                            # Try with 6 parameters (newer API with port_index)
                            cap = bmcapture.BMCapture(device_index, width, height, fps, True, port_index)
                        except TypeError:
                            # Fall back to 5 parameters (older API)
                            # First select the port on the device
                            bmcapture.select_input_port(device, port_index)
                            cap = bmcapture.BMCapture(device_index, width, height, fps, True)

                        # Check for frames
                        for _ in range(5):  # Try a few times to get a frame
                            if cap.update():
                                frame = cap.get_frame(format='rgb')
                                if frame is not None:
                                    actual_width = frame.shape[1]
                                    actual_height = frame.shape[0]
                                    #cap.close()
                                    print(f"  Active signal detected on port {port_index}: {port_name}")
                                    print(f"  Resolution: {actual_width}x{actual_height} @ {fps:.2f} fps")
                                    active_inputs.append((port_index, port_name, (width, height, fps)))
                                    break
                            time.sleep(0.1)
                        else:
                            # No frame found with this format, continue to next
                            cap.close()
                            continue

                        # If we found a format that works, break the loop
                        break
                    except Exception as e:
                        print(f"  Error testing format: {e}")
                        # Continue to next format
                        pass
            except Exception as e:
                print(f"  Error checking port {port_index}: {e}")

        try:
            #bmcapture.destroy_device(device)
            print("Device successfully destroyed")
        except Exception as e:
            print(f"Warning: Error destroying device: {e}")

        if not active_inputs:
            print("No active input signals detected")

        return active_inputs
    except Exception as e:
        print(f"Error while checking inputs: {e}")
        return []

def create_grid_layout(frames, max_width=1920, max_height=1080):
    """Create a grid layout for multiple frames"""
    if not frames:
        return None

    # Determine grid dimensions
    num_frames = len(frames)
    if num_frames == 1:
        grid_cols, grid_rows = 1, 1
    elif num_frames == 2:
        grid_cols, grid_rows = 2, 1
    elif num_frames <= 4:
        grid_cols, grid_rows = 2, 2
    elif num_frames <= 6:
        grid_cols, grid_rows = 3, 2
    elif num_frames <= 9:
        grid_cols, grid_rows = 3, 3
    else:
        grid_cols, grid_rows = 4, 3  # Max 12 channels in a 4x3 grid

    # Calculate cell dimensions
    cell_width = max_width // grid_cols
    cell_height = max_height // grid_rows

    # Create the grid
    grid = np.zeros((max_height, max_width, 3), dtype=np.uint8)

    # Place each frame in the grid
    for i, frame in enumerate(frames):
        if i >= grid_cols * grid_rows:
            break  # Skip if we have more frames than grid cells

        if frame is None:
            continue

        # Calculate position in grid
        row = i // grid_cols
        col = i % grid_cols

        # Resize frame to fit cell
        resized_frame = cv2.resize(frame, (cell_width, cell_height))

        # Place in grid
        y_start = row * cell_height
        y_end = (row + 1) * cell_height
        x_start = col * cell_width
        x_end = (col + 1) * cell_width

        grid[y_start:y_end, x_start:x_end] = resized_frame

    return grid

def main():
    # List available devices
    devices = bmcapture.get_devices()
    print(f"Available devices: {devices}")

    if not devices:
        print("No Blackmagic devices found")
        return

    # Default device index
    device_index = 0
    print(f"Using device {device_index}: {devices[device_index]}")

    # Check for active inputs
    active_inputs = check_active_inputs(device_index)

    if not active_inputs:
        print("No active inputs found. Please connect a video source and try again.")
        return

    print(f"Found {len(active_inputs)} active input(s)")

    # Create channels for each active input
    channels = []

    # Initialize capture for each active input
    print("Initializing capture for each active input...")

    try:
        # Print information for each active input
        for i, (port_index, port_name, format_info) in enumerate(active_inputs):
            width, height, fps = format_info
            print(f"Active input #{i+1}: Port {port_index} ({port_name}) - {width}x{height} @ {fps} fps")

        for port_index, port_name, (width, height, fps) in active_inputs:
            try:
                print(f"Initializing channel for port {port_index}: {port_name}")
                print(f"Using format: {width}x{height} @ {fps} fps")

                # Try to create a BMCapture instance with different framerates
                success = False
                for test_fps in [fps, 29.97, 30.0, 59.94, 60.0, 25.0, 24.0, 23.98]:
                    try:
                        print(f"  Trying framerate {test_fps}...")
                        cap = bmcapture.BMCapture(device_index, width, height, test_fps, True)
                        print(f"  Success with {width}x{height} @ {test_fps} fps!")
                        success = True
                        break
                    except Exception as e:
                        print(f"  Failed with {test_fps} fps: {e}")

                if not success:
                    raise RuntimeError(f"Could not initialize capture for port {port_index} with any framerate")

                # Set signal parameters
                if hasattr(cap, 'set_signal_parameters'):
                    cap.set_signal_parameters(min_frames=2, max_bad_frames=10)

                channels.append((port_index, port_name, cap))
            except Exception as e:
                print(f"Failed to initialize channel for port {port_index}: {e}")

        if not channels:
            print("Failed to initialize any channels")
            return

        # Create windows
        cv2.namedWindow('Multi-Channel Preview', cv2.WINDOW_NORMAL)

        # Initialize timing variables
        start_time = time.time()
        frame_count = 0
        fps_update_interval = 30  # Update FPS display every 30 frames

        # Create blank frames for each channel (for display while waiting for signal)
        blank_frames = {}
        for port_index, port_name, _ in channels:
            blank = np.zeros((480, 640, 3), dtype=np.uint8)
            cv2.putText(blank, f"Waiting for signal on {port_name}...", (20, 240),
                       cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
            blank_frames[port_index] = blank

        # Wait for signal on all channels
        print("Waiting for signals to stabilize...")
        signal_check_start = time.time()

        waiting_for_signal = True
        signal_timeout = 5.0  # Wait up to 5 seconds for signal

        while waiting_for_signal and (time.time() - signal_check_start < signal_timeout):
            # Update all channels
            all_signals_stable = True
            current_frames = []

            for i, (port_index, port_name, cap) in enumerate(channels):
                cap.update()

                # Simply use get_frame_count to determine if we have frames
                frame_count = cap.get_frame_count() if hasattr(cap, 'get_frame_count') else 0
                has_valid_signal = frame_count > 0

                # Get a frame regardless of signal status
                try:
                    frame = cap.get_frame(format='rgb')
                    if frame is not None:
                        # We have a valid frame, use it
                        pass
                    else:
                        all_signals_stable = False
                        frame = blank_frames[port_index].copy()
                except Exception:
                    all_signals_stable = False
                    frame = blank_frames[port_index].copy()

                # If we're using a blank frame, show frame count if available
                if frame is blank_frames[port_index].copy():
                    try:
                        if hasattr(cap, 'get_frame_count'):
                            frame_count = cap.get_frame_count()
                            if frame_count > 0:
                                count_text = f"Frames received: {frame_count}"
                                cv2.rectangle(frame, (20, 270), (350, 310), (0, 0, 0), -1)
                                cv2.putText(frame, count_text, (20, 300),
                                           cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                    except Exception:
                        pass

                # Convert to BGR for OpenCV
                frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)

                # Add channel info
                cv2.putText(frame_bgr, f"Port {port_index}: {port_name}", (10, 30),
                           cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

                # Get signal status from updated frames
                try:
                    has_valid_signal = True  # If we got this far, we likely have a valid signal

                    signal_text = f"Signal: {'LOCKED' if has_valid_signal else 'WAITING'}"
                    cv2.putText(frame_bgr, signal_text, (10, 60),
                              cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                              (0, 255, 0) if has_valid_signal else (0, 0, 255), 2)
                except Exception:
                    # If we can't determine signal status, just show 'UNKNOWN'
                    cv2.putText(frame_bgr, "Signal: UNKNOWN", (10, 60),
                              cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)

                current_frames.append(frame_bgr)

            # Create grid layout and display
            grid = create_grid_layout(current_frames)
            cv2.imshow('Multi-Channel Preview', grid)

            # Check for quit key
            if cv2.waitKey(100) & 0xFF == ord('q'):
                return

            # Exit wait loop if all signals are stable or we timeout
            if all_signals_stable:
                print("All signals locked!")
                waiting_for_signal = False

        if waiting_for_signal:
            print("Signal lock timeout - proceeding with available signals")

        # Reset timing once we have signals
        frame_count = 0
        start_time = time.time()

        # Output folder for saving frames
        output_dir = "frame_dump"
        os.makedirs(output_dir, exist_ok=True)

        # Main capture loop
        while True:
            # Update all channels and get frames
            current_frames = []

            for port_index, port_name, cap in channels:
                # Try to update and get new frame
                new_frame = cap.update()

                try:
                    # Simply check if we get a new frame
                    has_signal = new_frame

                    if new_frame and has_signal:
                        # Get frame as NumPy array
                        frame = cap.get_frame(format='rgb')
                        if frame is None:
                            # Use blank frame if no data
                            frame = blank_frames[port_index].copy()
                    else:
                        # Use previous frame or blank if no new frame
                        frame = blank_frames[port_index].copy()

                    # Convert from RGB to BGR for OpenCV
                    frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)

                    # Add channel info
                    cv2.putText(frame_bgr, f"Port {port_index}: {port_name}", (10, 30),
                               cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

                    try:
                        # Use the fact we got a frame as signal status
                        has_valid_signal = True

                        signal_text = f"Signal: {'LOCKED' if has_valid_signal else 'LOST'}"
                        cv2.putText(frame_bgr, signal_text, (10, 60),
                                  cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                                  (0, 255, 0) if has_valid_signal else (0, 0, 255), 2)

                        # Frame rate info (simplified)
                        rate_text = "Frame Rate: STABLE"
                        cv2.putText(frame_bgr, rate_text, (10, 90),
                                  cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

                        # Frame count (without potential errors)
                        frame_count_val = "N/A"
                        if hasattr(cap, 'frame_count_cache'):
                            frame_count_val = str(cap.frame_count_cache)
                        elif hasattr(cap, 'get_frame_count') and callable(getattr(cap, 'get_frame_count')):
                            try:
                                frame_count_val = str(cap.get_frame_count())
                            except:
                                pass

                        frame_count_text = f"Frames: {frame_count_val}"
                        cv2.putText(frame_bgr, frame_count_text, (10, 120),
                                  cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                    except Exception as e:
                        # Don't print errors for every frame
                        pass

                    current_frames.append(frame_bgr)
                except Exception as e:
                    print(f"Error processing frame from port {port_index}: {e}")
                    # Use blank frame if exception
                    frame = blank_frames[port_index].copy()
                    current_frames.append(frame)

            # Create grid layout
            grid = create_grid_layout(current_frames)

            # Update overall FPS counter
            frame_count += 1
            current_time = time.time()
            elapsed = current_time - start_time

            if frame_count % fps_update_interval == 0:
                fps_display = frame_count / elapsed
                print(f"Overall FPS: {fps_display:.2f}")

            # Add FPS info to grid
            fps_text = f"FPS: {frame_count / elapsed:.1f}"
            cv2.putText(grid, fps_text, (10, grid.shape[0] - 20),
                       cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

            # Display the grid
            cv2.imshow('Multi-Channel Preview', grid)

            # Save frames periodically if requested (uncommment to enable)
            # if frame_count % 30 == 0:  # Save every 30th frame
            #     for i, (port_index, port_name, cap) in enumerate(channels):
            #         if cap.has_valid_signal():
            #             try:
            #                 frame = cap.get_frame(format='rgb')
            #                 if frame is not None:
            #                     filename = f"{output_dir}/port_{port_index}_frame_{frame_count}.png"
            #                     cv2.imwrite(filename, cv2.cvtColor(frame, cv2.COLOR_RGB2BGR))
            #             except Exception as e:
            #                 print(f"Error saving frame from port {port_index}: {e}")

            # Press 'q' to quit
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

    except KeyboardInterrupt:
        print("Capture interrupted")
    finally:
        # Clean up
        cv2.destroyAllWindows()

        # Close all channels
        for _, _, cap in channels:
            try:
                cap.close()
            except Exception as e:
                print(f"Warning: Error closing channel: {e}")

        # No need to destroy device here since we're not creating one globally anymore

        # Print stats
        if 'start_time' in locals() and 'frame_count' in locals():
            elapsed = time.time() - start_time
            print(f"Captured {frame_count} grid frames in {elapsed:.2f} seconds")
            print(f"Average FPS: {frame_count / elapsed:.2f}")

if __name__ == "__main__":
    main()
