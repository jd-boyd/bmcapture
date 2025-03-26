#ifndef BMCAPTURE_H
#define BMCAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  // For size_t

/**
 * Main BMContext structure to hold the library's global state
 */
typedef struct BMContext BMContext;

/**
 * Device capture structure
 */
typedef struct BMCaptureDevice BMCaptureDevice;

/**
 * Channel identifier for multi-channel capture
 */
typedef struct BMCaptureChannel BMCaptureChannel;

typedef enum {
    BM_LOW_LATENCY = 75,    // 75ms timeout - for latency critical applications
    BM_NO_FRAME_DROPS = 500 // 500ms timeout - for frame critical applications
} BMCaptureMode;

typedef enum {
    BM_FORMAT_RGB,     // 3 channels, 8-bit RGB format
    BM_FORMAT_YUV,     // Raw YUV format (4:2:2)
    BM_FORMAT_GRAY     // 1 channel, 8-bit grayscale format
} BMPixelFormat;

/**
 * Create a new BlackMagic context.
 * This must be called before any other functions.
 * @return A new context, or NULL if initialization failed
 */
BMContext* bm_create_context(void);

/**
 * Free a BlackMagic context.
 * This should be called when done with the library to release resources.
 * @param context The context to free
 */
void bm_free_context(BMContext* context);

/**
 * Get the number of available devices.
 * @param context The library context
 * @return number of available devices
 */
int bm_get_device_count(BMContext* context);

/**
 * Get the name of a device.
 * @param context The library context
 * @param device_index Index of the device (0-based)
 * @param name_buffer Buffer to store the name
 * @param buffer_size Size of the buffer
 * @return true if successful, false otherwise
 */
bool bm_get_device_name(BMContext* context, int device_index, char* name_buffer, int buffer_size);

/**
 * Get the number of input ports available on a device.
 * @param context The library context
 * @param device_index Index of the device (0-based)
 * @return Number of input ports, or -1 if failed
 */
int bm_get_input_port_count(BMContext* context, int device_index);

/**
 * Get the name of an input port.
 * @param context The library context
 * @param device_index Index of the device (0-based)
 * @param port_index Index of the port (0-based)
 * @param name_buffer Buffer to store the name
 * @param buffer_size Size of the buffer
 * @return true if successful, false otherwise
 */
bool bm_get_input_port_name(BMContext* context, int device_index, int port_index, char* name_buffer, int buffer_size);

/**
 * Create a capture device.
 * @param context The library context
 * @param device_index Index of the device to use (0-based)
 * @return Handle to the capture device, or NULL if failed
 */
BMCaptureDevice* bm_create_device(BMContext* context, int device_index);

/**
 * Select an input port for the device.
 * @param context The library context
 * @param device Handle to the capture device
 * @param port_index Index of the port to use (0-based)
 * @return true if successful, false otherwise
 */
bool bm_select_input_port(BMContext* context, BMCaptureDevice* device, int port_index);

/**
 * Start capture with the specified format.
 * @param context The library context
 * @param device Handle to the capture device
 * @param width Desired width
 * @param height Desired height
 * @param framerate Desired framerate
 * @param mode Capture mode (latency vs frame integrity)
 * @return true if successful, false otherwise
 */
bool bm_start_capture(BMContext* context, BMCaptureDevice* device, int width, int height, float framerate, BMCaptureMode mode);

/**
 * Update the capture device and check for new frames.
 * @param context The library context
 * @param device Handle to the capture device
 * @return true if a new frame is available, false otherwise
 */
bool bm_update(BMContext* context, BMCaptureDevice* device);

/**
 * Get the latest captured frame.
 * @param context The library context
 * @param device Handle to the capture device
 * @param format Desired pixel format
 * @param buffer Pointer to the buffer to store the frame data
 * @param buffer_size Size of the buffer
 * @param out_width Optional pointer to store the width
 * @param out_height Optional pointer to store the height
 * @param out_channels Optional pointer to store the number of channels
 * @return true if successful, false otherwise
 */
bool bm_get_frame(BMContext* context, BMCaptureDevice* device, BMPixelFormat format, 
                  uint8_t* buffer, size_t buffer_size,
                  int* out_width, int* out_height, int* out_channels);

/**
 * Get the required buffer size for the specified format.
 * @param context The library context
 * @param device Handle to the capture device
 * @param format Desired pixel format
 * @return The required buffer size in bytes, or 0 if unknown
 */
size_t bm_get_frame_size(BMContext* context, BMCaptureDevice* device, BMPixelFormat format);

/**
 * Stop capture and release resources.
 * @param context The library context
 * @param device Handle to the capture device
 */
void bm_stop_capture(BMContext* context, BMCaptureDevice* device);

/**
 * Release device resources.
 * @param context The library context
 * @param device Handle to the capture device
 */
void bm_destroy_device(BMContext* context, BMCaptureDevice* device);

/**
 * Get the maximum number of input channels supported by this device.
 * @param context The library context
 * @param device Handle to the capture device
 * @return Number of supported channels, or -1 if failed
 */
int bm_get_channel_count(BMContext* context, BMCaptureDevice* device);

/**
 * Create a capture channel on a device.
 * A device can support multiple channels depending on hardware capabilities.
 * @param context The library context
 * @param device Handle to the capture device
 * @param port_index Index of the port to use (0-based)
 * @return Handle to the capture channel, or NULL if failed
 */
BMCaptureChannel* bm_create_channel(BMContext* context, BMCaptureDevice* device, int port_index);

/**
 * Start capture on a specific channel with the specified format.
 * @param context The library context
 * @param channel Handle to the capture channel
 * @param width Desired width
 * @param height Desired height
 * @param framerate Desired framerate
 * @param mode Capture mode (latency vs frame integrity)
 * @return true if successful, false otherwise
 */
bool bm_start_channel_capture(BMContext* context, BMCaptureChannel* channel, 
                             int width, int height, float framerate, BMCaptureMode mode);

/**
 * Update the capture channel and check for new frames.
 * @param context The library context
 * @param channel Handle to the capture channel
 * @return true if a new frame is available, false otherwise
 */
bool bm_update_channel(BMContext* context, BMCaptureChannel* channel);

/**
 * Get the latest captured frame from a channel.
 * @param context The library context
 * @param channel Handle to the capture channel
 * @param format Desired pixel format
 * @param buffer Pointer to the buffer to store the frame data
 * @param buffer_size Size of the buffer
 * @param out_width Optional pointer to store the width
 * @param out_height Optional pointer to store the height
 * @param out_channels Optional pointer to store the number of channels
 * @return true if successful, false otherwise
 */
bool bm_get_channel_frame(BMContext* context, BMCaptureChannel* channel, BMPixelFormat format, 
                         uint8_t* buffer, size_t buffer_size,
                         int* out_width, int* out_height, int* out_channels);

/**
 * Get the required buffer size for the specified format on a channel.
 * @param context The library context
 * @param channel Handle to the capture channel
 * @param format Desired pixel format
 * @return The required buffer size in bytes, or 0 if unknown
 */
size_t bm_get_channel_frame_size(BMContext* context, BMCaptureChannel* channel, BMPixelFormat format);

/**
 * Check if a channel has a valid signal with stable frames.
 * A valid signal means the device is receiving video data from the source.
 * @param context The library context
 * @param channel Handle to the capture channel
 * @return true if the signal is valid and stable, false otherwise
 */
bool bm_channel_has_valid_signal(BMContext* context, BMCaptureChannel* channel);

/**
 * Check if frames are being received at the expected rate.
 * This is useful to detect signal interruptions or degradation.
 * @param context The library context
 * @param channel Handle to the capture channel
 * @return true if frames are arriving at a consistent rate, false otherwise
 */
bool bm_channel_has_stable_frame_rate(BMContext* context, BMCaptureChannel* channel);

/**
 * Get the number of frames received since starting capture.
 * @param context The library context
 * @param channel Handle to the capture channel
 * @return Number of frames received, or 0 if no frames or error
 */
int bm_channel_get_frame_count(BMContext* context, BMCaptureChannel* channel);

/**
 * Set the parameters for signal detection.
 * @param context The library context
 * @param channel Handle to the capture channel
 * @param min_frames Minimum number of good frames required for signal lock (default: 3)
 * @param max_bad_frames Maximum number of bad frames before signal is considered lost (default: 5)
 * @return true if parameters were set successfully, false otherwise
 */
bool bm_channel_set_signal_parameters(BMContext* context, BMCaptureChannel* channel, 
                                     int min_frames, int max_bad_frames);

/**
 * Stop capture on a channel and release its resources.
 * @param context The library context
 * @param channel Handle to the capture channel
 */
void bm_stop_channel_capture(BMContext* context, BMCaptureChannel* channel);

/**
 * Release channel resources.
 * @param context The library context
 * @param channel Handle to the capture channel
 */
void bm_destroy_channel(BMContext* context, BMCaptureChannel* channel);

#ifdef __cplusplus
}
#endif

#endif /* BMCAPTURE_H */