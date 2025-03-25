# BlackMagic Python Capture Library

This Python module allows you to capture video frames from Blackmagic Design capture devices using the DeckLink SDK, and provides the frames as NumPy arrays for further processing.

## WARNING

So far it is tested only with the Decklink Duo 2 on macOS.  It should work on linux, and maybe windows, and it is expected to work in 1080p or lower mode on other cards, but anyhting other than 1080 on macOS on that one card is currently untested.

## Installation

Before installing this module, make sure you have:

1. Installed the [BlackMagic Desktop Video software](https://www.blackmagicdesign.com/support)
2. Verified your device is working with the official Black Magic software
3. Installed Python 3.10

```bash
pip install bmcapture
```


### From source

```bash
uv pip install -e ./
```

## Usage

```python
import bmcapture
import numpy as np
import cv2  # Optional, for display

# List available devices
devices = bmcapture.get_devices()
print(f"Available devices: {devices}")

# Initialize capture device
# Parameters: device_index, width, height, framerate, low_latency
cap = bmcapture.BMCapture(0, 1920, 1080, 30.0, True)

try:
    while True:
        # Check for new frame
        if cap.update():
            # Get frame as NumPy array (format can be 'rgb', 'yuv', or 'gray')
            frame = cap.get_frame(format='rgb')

            # Use with OpenCV (optional)
            # Convert from RGB to BGR for OpenCV
            frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
            cv2.imshow('BlackMagic Capture', frame_bgr)

            # Process frame with NumPy
            # Example: calculate mean color
            mean_color = np.mean(frame, axis=(0, 1))
            print(f"Mean RGB: {mean_color}")

        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

finally:
    # Clean up
    cap.close()
    cv2.destroyAllWindows()
```

## Performance Tips

1. Use the 'yuv' or 'gray' formats when possible, as they're faster than RGB conversion:

```python
# For grayscale processing
gray_frame = cap.get_frame(format='gray')

# For YUV processing (advanced users)
yuv_frame = cap.get_frame(format='yuv')
```

2. Set `low_latency=False` for more reliable frame capture at the expense of latency:

```python
# Prioritize never dropping frames over latency
cap = bmcapture.BMCapture(0, 1920, 1080, 30.0, low_latency=False)
```

## Technical Information

- Frames are provided in numpy arrays with the following formats:
  - RGB: shape=(height, width, 3), dtype=uint8
  - YUV: shape=(height, width/2, 4), dtype=uint8 (4:2:2 format as cb-y0-cr-y1)
  - Gray: shape=(height, width), dtype=uint8

- The library uses triple buffering to provide the latest frame with minimal latency.

- YUV to RGB conversion uses optimized lookup tables for performance.

## Credit

Making this work was heavily cribbed from:
https://github.com/kylemcdonald/ofxBlackmagic
