"""
Preview example showing how to use bmcapture with OpenCV.
Automatically detects active inputs and displays the video feed.
"""

import bmcapture
import numpy as np
import cv2
import time

def check_active_input(device_index=0):
    """Find active input port and format"""
    print("Checking for active inputs...")

    try:
        # Get input ports for device
        ports = bmcapture.get_input_ports(device_index)
        if not ports:
            print("No input ports found")
            return None

        print(f"Found {len(ports)} input port(s):")

        # Check each port for an active signal
        for port_index, port_name in enumerate(ports):
            print(f"Port {port_index}: {port_name}")

            try:
                # Create device and select port
                device = bmcapture.create_device(device_index)
                if not device:
                    continue

                if not bmcapture.select_input_port(device, port_index):
                    bmcapture.destroy_device(device)
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
                        cap = bmcapture.BMCapture(device_index, width, height, fps, True)

                        # Check for frames
                        for _ in range(3):
                            if cap.update():
                                frame = cap.get_frame(format='rgb')
                                if frame is not None:
                                    actual_width = frame.shape[1]
                                    actual_height = frame.shape[0]
                                    cap.close()
                                    # Don't destroy device here, it's already managed by BMCapture
                                    print(f"  Active signal detected on port {port_index}: {port_name}")
                                    print(f"  Resolution: {actual_width}x{actual_height} @ {fps:.2f} fps")
                                    return port_index, (width, height, fps)
                            time.sleep(0.1)

                        cap.close()
                    except Exception:
                        # Continue to next format
                        pass

                bmcapture.destroy_device(device)
            except Exception as e:
                print(f"  Error checking port {port_index}: {e}")

        print("No active input signal detected")
        return None
    except Exception as e:
        print(f"Error while checking inputs: {e}")
        return None

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

    time.sleep(5)

    # Check for active input
    active_input = check_active_input(device_index)

    # Initialize capture
    if active_input:
        port_index, (width, height, fps) = active_input
        try:
            print(f"Initializing capture with detected format: {width}x{height} @ {fps} fps")
            cap = bmcapture.BMCapture(device_index, width, height, fps, True)
        except Exception as e:
            print(f"Failed to initialize with detected format: {e}")
            print("Trying default format instead...")
            cap = bmcapture.BMCapture(0, 1920, 1080, 30.0, True)
    else:
        print("Using default format: 1920x1080 @ 30.0 fps")
        cap = bmcapture.BMCapture(0, 1920, 1080, 30.0, True)

    frame_count = 0
    start_time = time.time()
    fps_update_interval = 30  # Update FPS display every 30 frames

    try:
        # Wait for signal lock
        waiting_for_signal = True
        signal_check_start = time.time()

        print("Waiting for valid video signal...")
        # Create a blank frame to show while waiting
        blank_frame = np.zeros((480, 640, 3), dtype=np.uint8)
        cv2.putText(blank_frame, "Waiting for video signal...", (50, 240),
                   cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 255), 2)

        while waiting_for_signal:
            # Display the waiting message
            cv2.imshow('BlackMagic Capture', blank_frame)

            # Check if we have a valid signal
            if cap.has_valid_signal():
                print(f"Signal locked after {cap.get_frame_count()} frames!")
                waiting_for_signal = False
                break

            # Show frame count while waiting
            if cap.get_frame_count() > 0:
                count_text = f"Frames received: {cap.get_frame_count()}"
                # Clear previous text
                cv2.rectangle(blank_frame, (50, 270), (400, 310), (0, 0, 0), -1)
                cv2.putText(blank_frame, count_text, (50, 300),
                           cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

            # Check for quit key
            if cv2.waitKey(100) & 0xFF == ord('q'):
                return

            # Add a timeout to avoid infinite wait
            if time.time() - signal_check_start > 5.0:
                print("Warning: Timeout waiting for signal lock, proceeding anyway")
                break

        # Reset timing once we have signal
        frame_count = 0
        start_time = time.time()

        while True:
            # Wait for a new frame to become available
            new_frame = False
            for _ in range(10):  # Try for a short time
                new_frame = cap.update()
                if new_frame:
                    break
                # Press 'q' to quit, check during frame wait
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    return
                time.sleep(0.01)  # 10ms sleep between checks

            if new_frame:
                try:
                    # Get frame as NumPy array
                    frame = cap.get_frame(format='rgb')
                    frame_count += 1

                    # Convert from RGB to BGR for OpenCV
                    frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)

                    # # Calculate FPS
                    # current_time = time.time()
                    # elapsed = current_time - start_time
                    # if frame_count % fps_update_interval == 0:
                    #     fps_display = frame_count / elapsed
                    #     print(f"Current FPS: {fps_display:.2f}")

                    # # Add resolution and FPS info to frame
                    # resolution_text = f"{frame.shape[1]}x{frame.shape[0]}"
                    # cv2.putText(frame_bgr, resolution_text, (10, 30),
                    #            cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)

                    # fps_text = f"FPS: {frame_count / elapsed:.1f}"
                    # cv2.putText(frame_bgr, fps_text, (10, 70),
                    #            cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)

                    # # Add signal and frame count info
                    # signal_text = f"Signal: {'LOCKED' if cap.has_valid_signal() else 'UNSTABLE'}"
                    # cv2.putText(frame_bgr, signal_text, (10, 110),
                    #            cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                    #            (0, 255, 0) if cap.has_valid_signal() else (0, 0, 255), 2)

                    # count_text = f"Device frame count: {cap.get_frame_count()}"
                    # cv2.putText(frame_bgr, count_text, (10, 150),
                    #            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

                    # Display the frame
                    cv2.imshow('BlackMagic Capture', frame_bgr)
                except Exception as e:
                    print(f"Error processing frame: {e}")

            # Press 'q' to quit
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

    except KeyboardInterrupt:
        print("Capture interrupted")
    finally:
        # Clean up
        elapsed = time.time() - start_time
        print(f"Captured {frame_count} frames in {elapsed:.2f} seconds")
        print(f"Average FPS: {frame_count / elapsed:.2f}")
        cap.close()
        cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
