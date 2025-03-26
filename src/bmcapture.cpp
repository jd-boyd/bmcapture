#include "bmcapture.h"
#include "DeckLinkAPI.h"
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <mutex>
#include <condition_variable>

// Forward declarations for C++ implementation
struct BMCaptureChannel;

// Callback class for DeckLink API
class BMCaptureCallback : public IDeckLinkInputCallback {
private:
    BMCaptureChannel* channel; // Internal channel used for backward compatibility

public:
    BMCaptureCallback() : channel(nullptr) {}

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv) override {
        return E_NOINTERFACE;
    }

    virtual ULONG STDMETHODCALLTYPE AddRef() override {
        return 1;
    }

    virtual ULONG STDMETHODCALLTYPE Release() override {
        return 1;
    }

    virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(
        BMDVideoInputFormatChangedEvents notificationEvents,
        IDeckLinkDisplayMode* newDisplayMode,
        BMDDetectedVideoInputFormatFlags detectedSignalFlags) override {
        // Format change detected - could update parameters here
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(
        IDeckLinkVideoInputFrame* videoFrame,
        IDeckLinkAudioInputPacket* audioPacket) override;

    void setChannel(BMCaptureChannel* ch) {
        channel = ch;
    }
};

// Structure to store YUV -> RGB lookup tables for color conversion
struct YUVConversionTables {
    bool initialized = false;
    uint8_t red[256][256];         // [v][y]
    uint8_t green[256][256][256];  // [u][v][y]
    uint8_t blue[256][256];        // [u][y]
};

// Structure for a captured frame
struct CapturedFrame {
    std::vector<uint8_t> yuv_data;
    std::vector<uint8_t> rgb_data;
    std::vector<uint8_t> gray_data;
    bool rgb_updated = false;
    bool gray_updated = false;
    std::timed_mutex* mutex;  // Use a pointer to the mutex
    int width = 0;
    int height = 0;

    // Default constructor initializes the mutex
    CapturedFrame() : mutex(new std::timed_mutex()) {}

    // Move constructor
    CapturedFrame(CapturedFrame&& other) noexcept :
        yuv_data(std::move(other.yuv_data)),
        rgb_data(std::move(other.rgb_data)),
        gray_data(std::move(other.gray_data)),
        rgb_updated(other.rgb_updated),
        gray_updated(other.gray_updated),
        width(other.width),
        height(other.height) {
        mutex = other.mutex;
        other.mutex = nullptr;  // Transfer ownership
    }

    // Move assignment operator
    CapturedFrame& operator=(CapturedFrame&& other) noexcept {
        if (this != &other) {
            yuv_data = std::move(other.yuv_data);
            rgb_data = std::move(other.rgb_data);
            gray_data = std::move(other.gray_data);
            rgb_updated = other.rgb_updated;
            gray_updated = other.gray_updated;
            width = other.width;
            height = other.height;

            // Handle the mutex
            delete mutex;
            mutex = other.mutex;
            other.mutex = nullptr;  // Transfer ownership
        }
        return *this;
    }

    // Destructor to clean up the mutex
    ~CapturedFrame() {
        delete mutex;
    }

    // Delete copy constructor and assignment operator
    CapturedFrame(const CapturedFrame&) = delete;
    CapturedFrame& operator=(const CapturedFrame&) = delete;
};

// Triple buffer implementation
template <typename T>
class TripleBuffer {
private:
    T buffers[3];
    int back = 0;
    int middle = 1;
    int front = 2;
    std::mutex mutex;

public:
    bool swapBack(T& data) {
        std::lock_guard<std::mutex> lock(mutex);
        // Move the data to the back buffer
        buffers[back] = std::move(data);
        std::swap(back, middle);
        return true;
    }

    bool swapFront() {
        std::lock_guard<std::mutex> lock(mutex);
        std::swap(middle, front);
        return true;
    }

    T& getFront() {
        return buffers[front];
    }
};

// Main context for the library
struct BMContext {
    // We might add more global state here in the future
    IDeckLinkIterator* iterator;

    BMContext() : iterator(nullptr) {
        // Initialize DeckLink SDK here
        iterator = CreateDeckLinkIteratorInstance();
    }

    ~BMContext() {
        if (iterator) {
            iterator->Release();
            iterator = nullptr;
        }
        // Cleanup other resources if needed
    }
};

// Forward declarations
struct BMCaptureChannel;
struct BMCaptureDevice;

// Implementation of the BMCaptureDevice
struct BMCaptureDevice {
    IDeckLink* device = nullptr;
    IDeckLinkInput* input = nullptr;
    BMCaptureCallback* callback = nullptr;
    std::vector<BMCaptureChannel*> channels;  // Store all channels associated with this device
    TripleBuffer<CapturedFrame> buffer;
    YUVConversionTables yuv_tables;
    int width = 0;
    int height = 0;
    bool capturing = false;
    BMCaptureMode capture_mode = BM_LOW_LATENCY;

    BMCaptureDevice() = default;

    ~BMCaptureDevice() {
        fprintf(stderr, "E.1\n");
        // Channels should be deleted by the caller
        if (input) {
            input->StopStreams();
            input->DisableVideoInput();
            input->SetCallback(nullptr);
            input->Release();
            input = nullptr;
        }
        fprintf(stderr, "E.2\n");
        // Clean up callback object
        delete callback;
        callback = nullptr;
        fprintf(stderr, "E.3\n");

        // if (device) {
        //     fprintf(stderr, "E.3.1\n");
        //     device->Release();
        //     fprintf(stderr, "E.3.2\n");
        //     device = nullptr;
        //     fprintf(stderr, "E.3.3\n");
        // }
        fprintf(stderr, "E.4\n");
    }
};

// Channel callback class for DeckLink SDK
class BMChannelCallback : public IDeckLinkInputCallback {
private:
    BMCaptureChannel* channel;

public:
    BMChannelCallback(BMCaptureChannel* ch) : channel(ch) {}

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv) override {
        return E_NOINTERFACE;
    }

    virtual ULONG STDMETHODCALLTYPE AddRef() override {
        return 1;
    }

    virtual ULONG STDMETHODCALLTYPE Release() override {
        return 1;
    }

    virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(
        BMDVideoInputFormatChangedEvents notificationEvents,
        IDeckLinkDisplayMode* newDisplayMode,
        BMDDetectedVideoInputFormatFlags detectedSignalFlags) override;

    virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(
        IDeckLinkVideoInputFrame* videoFrame,
        IDeckLinkAudioInputPacket* audioPacket) override;
};

// Implementation of a capture channel
#include <algorithm> // For std::remove
#include <chrono>

struct BMCaptureChannel {
    BMCaptureDevice* parent_device = nullptr;
    IDeckLinkInput* input = nullptr;
    BMChannelCallback* callback = nullptr;
    TripleBuffer<CapturedFrame> buffer;
    YUVConversionTables yuv_tables;
    int width = 0;
    int height = 0;
    int port_index = 0;
    bool capturing = false;
    bool signal_locked = false;
    int signal_stable_count = 0;     // Count of consecutive valid frames
    int signal_lost_count = 0;       // Count of consecutive invalid frames
    int frame_count = 0;
    int min_frames_for_lock = 3;     // Minimum frames needed for stable signal
    int max_lost_frames = 5;         // Maximum lost frames before signal is considered unstable
    std::chrono::time_point<std::chrono::steady_clock> last_frame_time;
    BMCaptureMode capture_mode = BM_LOW_LATENCY;

    BMCaptureChannel(BMCaptureDevice* device, int port)
        : parent_device(device), port_index(port) {
        callback = new BMChannelCallback(this);
        last_frame_time = std::chrono::steady_clock::now();
    }

    ~BMCaptureChannel() {
        if (capturing) {
            // Stop capture if still running
            if (input) {
                input->StopStreams();
                input->DisableVideoInput();
                input->SetCallback(nullptr);
                input->Release();
                input = nullptr;
            }
            capturing = false;
        }

        delete callback;
    }

    // Check if the channel has a locked signal with valid frames
    bool hasValidSignal() const {
        // Signal is considered locked if:
        // 1. We've received at least min_frames_for_lock consecutive valid frames
        // 2. The current signal status is good
        return signal_locked && signal_stable_count >= min_frames_for_lock;
    }

    // Update the signal status
    void updateSignalStatus(bool has_valid_frame) {
        // Update the last frame time
        last_frame_time = std::chrono::steady_clock::now();

        if (has_valid_frame) {
            // Valid frame received
            signal_stable_count++;
            signal_lost_count = 0;

            // Lock signal after receiving enough good frames
            if (signal_stable_count >= min_frames_for_lock) {
                signal_locked = true;
            }
        } else {
            // Invalid frame received
            signal_lost_count++;
            signal_stable_count = 0;

            // Lose signal lock after several bad frames
            if (signal_lost_count >= max_lost_frames) {
                signal_locked = false;
            }
        }
    }

    // Prime the buffer to make frames available faster at startup
    void primeBuffer(CapturedFrame& frame) {
        // Create a deep copy of the frame for each buffer to avoid reference issues
        CapturedFrame back_frame = createFrameCopy(frame);
        buffer.swapBack(back_frame);

        // Move to front buffer right away - triple buffer design needs this
        buffer.swapFront();
    }

    // Create a deep copy of a frame
    CapturedFrame createFrameCopy(const CapturedFrame& src) {
        CapturedFrame copy;

        // Copy basic properties
        copy.width = src.width;
        copy.height = src.height;
        copy.rgb_updated = src.rgb_updated;
        copy.gray_updated = src.gray_updated;

        // Deep copy of data buffers
        if (!src.yuv_data.empty()) {
            copy.yuv_data = src.yuv_data; // std::vector handles deep copy
        }

        if (!src.rgb_data.empty()) {
            copy.rgb_data = src.rgb_data;
        }

        if (!src.gray_data.empty()) {
            copy.gray_data = src.gray_data;
        }

        return copy;
    }

    // Check if we're getting frames at the expected rate
    bool isFrameRateStable() const {
        if (frame_count < 10) return false; // Need minimum frames to determine

        // Check if we're getting frames at a reasonable rate
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_frame_time).count();

        // If last frame was more than 500ms ago, frame rate is unstable
        return elapsed < 500;
    }
};

// BMCaptureCallback implementation is now defined above

// Implement the BMCaptureCallback VideoInputFrameArrived method
HRESULT STDMETHODCALLTYPE BMCaptureCallback::VideoInputFrameArrived(
    IDeckLinkVideoInputFrame* videoFrame,
    IDeckLinkAudioInputPacket* audioPacket) {

    if (videoFrame == nullptr) {
        return S_OK;
    }

    // Get frame dimensions
    long width = videoFrame->GetWidth();
    long height = videoFrame->GetHeight();
    long rowBytes = videoFrame->GetRowBytes();

    if (channel != nullptr) {
        if (width != channel->width || height != channel->height) {
            channel->width = width;
            channel->height = height;
        }
    }

    // Get frame data
    void* frameBytes;
    if (videoFrame->GetBytes(&frameBytes) != S_OK) {
        return S_OK;
    }

    // Prepare captured frame
    CapturedFrame frame;
    frame.width = width;
    frame.height = height;

    // Copy YUV data
    size_t dataSize = height * rowBytes;
    frame.yuv_data.resize(dataSize);
    memcpy(frame.yuv_data.data(), frameBytes, dataSize);

    // Mark RGB and gray data as needing update
    frame.rgb_updated = false;
    frame.gray_updated = false;

    // Add to triple buffer using move semantics
    if (channel != nullptr) {
        channel->buffer.swapBack(frame);
    }

    return S_OK;
}

// Implement the channel callback methods
HRESULT STDMETHODCALLTYPE BMChannelCallback::VideoInputFormatChanged(
    BMDVideoInputFormatChangedEvents notificationEvents,
    IDeckLinkDisplayMode* newDisplayMode,
    BMDDetectedVideoInputFormatFlags detectedSignalFlags) {
    // Format change detected - could update parameters here
    return S_OK;
}

HRESULT STDMETHODCALLTYPE BMChannelCallback::VideoInputFrameArrived(
    IDeckLinkVideoInputFrame* videoFrame,
    IDeckLinkAudioInputPacket* audioPacket) {

    if (videoFrame == nullptr || channel == nullptr) {
        return S_OK;
    }

    // Check frame flags to determine if we have a valid signal
    BMDFrameFlags flags = videoFrame->GetFlags();
    bool has_valid_frame = !(flags & bmdFrameHasNoInputSource);

    // Update our signal status tracking
    channel->updateSignalStatus(has_valid_frame);

    // Increment frame counter - useful for startup synchronization
    channel->frame_count++;

    // Get frame dimensions
    long width = videoFrame->GetWidth();
    long height = videoFrame->GetHeight();
    long rowBytes = videoFrame->GetRowBytes();

    if (width != channel->width || height != channel->height) {
        channel->width = width;
        channel->height = height;
    }

    // Get frame data
    void* frameBytes;
    if (videoFrame->GetBytes(&frameBytes) != S_OK || frameBytes == nullptr) {
        // Cannot get frame data
        return S_OK;
    }

    // Prepare captured frame
    CapturedFrame frame;
    frame.width = width;
    frame.height = height;

    // Copy YUV data
    size_t dataSize = height * rowBytes;
    frame.yuv_data.resize(dataSize);
    memcpy(frame.yuv_data.data(), frameBytes, dataSize);

    // Mark RGB and gray data as needing update
    frame.rgb_updated = false;
    frame.gray_updated = false;

    // Add to triple buffer using move semantics
    channel->buffer.swapBack(frame);

    // For the first few frames, also prime the buffer to make frames available immediately
    if (channel->frame_count <= channel->min_frames_for_lock) {
        // Prime the middle and front buffers too
        channel->primeBuffer(frame);
    }

    return S_OK;
}

// Utility functions for color conversion
static uint8_t clamp(int value) {
    if (value > 255) return 255;
    if (value < 0) return 0;
    return (uint8_t)value;
}

static void initialize_yuv_tables(YUVConversionTables* tables) {
    if (tables->initialized) {
        return;
    }

    int yy, uu, vv, ug_plus_vg, ub, vr, val;

    // Generate red component lookup table [v][y]
    for (int y = 0; y < 256; y++) {
        for (int v = 0; v < 256; v++) {
            yy = y << 8;
            vv = v - 128;
            vr = vv * 359;
            val = (yy + vr) >> 8;
            tables->red[v][y] = clamp(val);
        }
    }

    // Generate green component lookup table [u][v][y]
    for (int y = 0; y < 256; y++) {
        for (int u = 0; u < 256; u++) {
            for (int v = 0; v < 256; v++) {
                yy = y << 8;
                uu = u - 128;
                vv = v - 128;
                ug_plus_vg = uu * 88 + vv * 183;
                val = (yy - ug_plus_vg) >> 8;
                tables->green[u][v][y] = clamp(val);
            }
        }
    }

    // Generate blue component lookup table [u][y]
    for (int y = 0; y < 256; y++) {
        for (int u = 0; u < 256; u++) {
            yy = y << 8;
            uu = u - 128;
            ub = uu * 454;
            val = (yy + ub) >> 8;
            tables->blue[u][y] = clamp(val);
        }
    }

    tables->initialized = true;
}

static void yuv_to_gray(const uint8_t* yuv, uint8_t* gray, unsigned int pixel_count) {
    // YUV is in cb-y0-cr-y1 format, extract only y values
    for (unsigned int i = 0, j = 1; i < pixel_count; i++, j += 2) {
        gray[i] = yuv[j];
    }
}

static void yuv_to_rgb(const uint8_t* yuv, uint8_t* rgb, unsigned int pixel_count, YUVConversionTables* tables) {
    initialize_yuv_tables(tables);

    uint8_t u, y0, v, y1;
    unsigned int yuv_size = 2 * pixel_count;

    for (unsigned int i = 0, j = 0; i < yuv_size; i += 4, j += 6) {
        u = yuv[i+0];
        y0 = yuv[i+1];
        v = yuv[i+2];
        y1 = yuv[i+3];

        rgb[j+0] = tables->red[v][y0];      // R0
        rgb[j+1] = tables->green[u][v][y0]; // G0
        rgb[j+2] = tables->blue[u][y0];     // B0

        rgb[j+3] = tables->red[v][y1];      // R1
        rgb[j+4] = tables->green[u][v][y1]; // G1
        rgb[j+5] = tables->blue[u][y1];     // B1
    }
}

// Implementation of the API functions

BMContext* bm_create_context(void) {
    BMContext* context = new BMContext();
    if (context->iterator == nullptr) {
        delete context;
        return nullptr;
    }
    return context;
}

void bm_free_context(BMContext* context) {
    if (context) {
        delete context;
    }
}

int bm_get_device_count(BMContext* context) {
    if (context == nullptr || context->iterator == nullptr) {
        return 0;
    }

    int count = 0;
    IDeckLink* device = nullptr;
    IDeckLinkIterator* iterator = context->iterator;

    // Reset the iterator to the beginning
    iterator->Release();
    context->iterator = CreateDeckLinkIteratorInstance();
    iterator = context->iterator;

    if (iterator == nullptr) {
        return 0;
    }

    while (iterator->Next(&device) == S_OK) {
        count++;
        device->Release();
    }

    return count;
}

bool bm_get_device_name(BMContext* context, int device_index, char* name_buffer, int buffer_size) {
    if (context == nullptr || name_buffer == nullptr || buffer_size <= 0) {
        return false;
    }

    IDeckLinkIterator* iterator = context->iterator;
    if (iterator == nullptr) {
        return false;
    }

    // Reset the iterator to the beginning
    iterator->Release();
    context->iterator = CreateDeckLinkIteratorInstance();
    iterator = context->iterator;

    if (iterator == nullptr) {
        return false;
    }

    IDeckLink* device = nullptr;
    int current_index = 0;
    bool success = false;

    while (iterator->Next(&device) == S_OK) {
        if (current_index == device_index) {
            CFStringRef deviceName;
            if (device->GetDisplayName(&deviceName) == S_OK) {
                const char* name = CFStringGetCStringPtr(deviceName, kCFStringEncodingMacRoman);
                if (name != nullptr) {
                    strncpy(name_buffer, name, buffer_size - 1);
                    name_buffer[buffer_size - 1] = '\0';
                    success = true;
                }
                CFRelease(deviceName);
            }
            device->Release();
            break;
        }

        device->Release();
        current_index++;
    }

    return success;
}

int bm_get_input_port_count(BMContext* context, int device_index) {
    if (context == nullptr) {
        return -1;
    }

    IDeckLinkIterator* iterator = context->iterator;
    if (iterator == nullptr) {
        return -1;
    }

    // Reset the iterator to the beginning
    iterator->Release();
    context->iterator = CreateDeckLinkIteratorInstance();
    iterator = context->iterator;

    if (iterator == nullptr) {
        return -1;
    }

    IDeckLink* device = nullptr;
    int current_index = 0;
    int count = 0;

    while (iterator->Next(&device) == S_OK) {
        if (current_index == device_index) {
            IDeckLinkAttributes* deckLinkAttributes = nullptr;
            int64_t connectionsValue = 0;

            // Get attributes interface
            if (device->QueryInterface(IID_IDeckLinkAttributes, (void**)&deckLinkAttributes) == S_OK) {
                // Get available input connections
                if (deckLinkAttributes->GetInt(BMDDeckLinkVideoInputConnections, &connectionsValue) == S_OK) {
                    // Convert to BMDVideoConnection
                    BMDVideoConnection connections = (BMDVideoConnection)connectionsValue;

                    // Count available connections
                    if (connections & bmdVideoConnectionSDI)
                        count++;
                    if (connections & bmdVideoConnectionHDMI)
                        count++;
                    if (connections & bmdVideoConnectionOpticalSDI)
                        count++;
                    if (connections & bmdVideoConnectionComponent)
                        count++;
                    if (connections & bmdVideoConnectionComposite)
                        count++;
                    if (connections & bmdVideoConnectionSVideo)
                        count++;
                }
                deckLinkAttributes->Release();
            }
            device->Release();
            break;
        }

        device->Release();
        current_index++;
    }

    return count;
}

bool bm_get_input_port_name(BMContext* context, int device_index, int port_index, char* name_buffer, int buffer_size) {
    if (context == nullptr || name_buffer == nullptr || buffer_size <= 0) {
        return false;
    }

    IDeckLinkIterator* iterator = context->iterator;
    if (iterator == nullptr) {
        return false;
    }

    // Reset the iterator to the beginning
    iterator->Release();
    context->iterator = CreateDeckLinkIteratorInstance();
    iterator = context->iterator;

    if (iterator == nullptr) {
        return false;
    }

    IDeckLink* device = nullptr;
    int current_index = 0;
    bool success = false;

    while (iterator->Next(&device) == S_OK) {
        if (current_index == device_index) {
            IDeckLinkAttributes* deckLinkAttributes = nullptr;
            int64_t connectionsValue = 0;

            // Get attributes interface
            if (device->QueryInterface(IID_IDeckLinkAttributes, (void**)&deckLinkAttributes) == S_OK) {
                // Get available input connections
                if (deckLinkAttributes->GetInt(BMDDeckLinkVideoInputConnections, &connectionsValue) == S_OK) {
                    // Convert to BMDVideoConnection
                    BMDVideoConnection connections = (BMDVideoConnection)connectionsValue;

                    // Create a vector of port names
                    std::vector<std::string> portNames;
                    if (connections & bmdVideoConnectionSDI)
                        portNames.push_back("SDI");
                    if (connections & bmdVideoConnectionHDMI)
                        portNames.push_back("HDMI");
                    if (connections & bmdVideoConnectionOpticalSDI)
                        portNames.push_back("Optical SDI");
                    if (connections & bmdVideoConnectionComponent)
                        portNames.push_back("Component");
                    if (connections & bmdVideoConnectionComposite)
                        portNames.push_back("Composite");
                    if (connections & bmdVideoConnectionSVideo)
                        portNames.push_back("S-Video");

                    // Get the name for the requested port
                    if (port_index >= 0 && port_index < (int)portNames.size()) {
                        strncpy(name_buffer, portNames[port_index].c_str(), buffer_size - 1);
                        name_buffer[buffer_size - 1] = '\0';
                        success = true;
                    }
                }
                deckLinkAttributes->Release();
            }
            device->Release();
            break;
        }

        device->Release();
        current_index++;
    }

    return success;
}

BMCaptureDevice* bm_create_device(BMContext* context, int device_index) {
    if (context == nullptr) {
        return nullptr;
    }

    IDeckLinkIterator* iterator = context->iterator;
    if (iterator == nullptr) {
        return nullptr;
    }

    // Reset the iterator to the beginning
    iterator->Release();
    context->iterator = CreateDeckLinkIteratorInstance();
    iterator = context->iterator;

    if (iterator == nullptr) {
        return nullptr;
    }

    IDeckLink* device = nullptr;
    int current_index = 0;

    while (iterator->Next(&device) == S_OK) {
        if (current_index == device_index) {
            break;
        }

        device->Release();
        device = nullptr;
        current_index++;
    }

    if (device == nullptr) {
        return nullptr;
    }

    // Create the capture device structure
    BMCaptureDevice* capture_device = new BMCaptureDevice();
    capture_device->device = device;
    capture_device->callback = new BMCaptureCallback();

    return capture_device;
}

bool bm_select_input_port(BMContext* context, BMCaptureDevice* device, int port_index) {
    if (context == nullptr || device == nullptr || device->device == nullptr) {
        return false;
    }

    // Get port names for this device
    IDeckLinkAttributes* deckLinkAttributes = nullptr;
    IDeckLinkConfiguration* deckLinkConfig = nullptr;
    int64_t connectionsValue = 0;
    BMDVideoConnection selectedConnection = 0;
    bool success = false;

    // Get attributes interface
    if (device->device->QueryInterface(IID_IDeckLinkAttributes, (void**)&deckLinkAttributes) != S_OK) {
        return false;
    }

    // Get configuration interface
    if (device->device->QueryInterface(IID_IDeckLinkConfiguration, (void**)&deckLinkConfig) != S_OK) {
        deckLinkAttributes->Release();
        return false;
    }

    // Get available input connections
    if (deckLinkAttributes->GetInt(BMDDeckLinkVideoInputConnections, &connectionsValue) != S_OK) {
        deckLinkAttributes->Release();
        deckLinkConfig->Release();
        return false;
    }

    // Convert to BMDVideoConnection
    BMDVideoConnection connections = (BMDVideoConnection)connectionsValue;

    // Create a vector of port connections
    std::vector<BMDVideoConnection> portConnections;
    if (connections & bmdVideoConnectionSDI)
        portConnections.push_back(bmdVideoConnectionSDI);
    if (connections & bmdVideoConnectionHDMI)
        portConnections.push_back(bmdVideoConnectionHDMI);
    if (connections & bmdVideoConnectionOpticalSDI)
        portConnections.push_back(bmdVideoConnectionOpticalSDI);
    if (connections & bmdVideoConnectionComponent)
        portConnections.push_back(bmdVideoConnectionComponent);
    if (connections & bmdVideoConnectionComposite)
        portConnections.push_back(bmdVideoConnectionComposite);
    if (connections & bmdVideoConnectionSVideo)
        portConnections.push_back(bmdVideoConnectionSVideo);

    // Get the connection for the requested port
    if (port_index >= 0 && port_index < (int)portConnections.size()) {
        selectedConnection = portConnections[port_index];

        // Set input connection
        if (deckLinkConfig->SetInt(bmdDeckLinkConfigVideoInputConnection, selectedConnection) == S_OK) {
            success = true;
        }
    }

    deckLinkAttributes->Release();
    deckLinkConfig->Release();
    return success;
}

bool bm_start_capture(BMContext* context, BMCaptureDevice* device, int width, int height, float framerate, BMCaptureMode mode) {
    if (context == nullptr || device == nullptr || device->device == nullptr) {
        fprintf(stderr, "Error: Invalid device handle\n");
        return false;
    }

    if (device->capturing) {
        bm_stop_capture(context, device);
    }

    device->width = width;
    device->height = height;
    device->capture_mode = mode;

    // Get the IDeckLinkInput interface
    HRESULT result = device->device->QueryInterface(IID_IDeckLinkInput, (void**)&device->input);
    if (result != S_OK) {
        fprintf(stderr, "Error: Failed to get DeckLink input interface (error code: %ld)\n", (long)result);
        return false;
    }

    // Set the callback
    device->input->SetCallback(device->callback);

    // Find the appropriate display mode
    IDeckLinkDisplayModeIterator* display_mode_iterator = nullptr;
    result = device->input->GetDisplayModeIterator(&display_mode_iterator);
    if (result != S_OK) {
        fprintf(stderr, "Error: Failed to get display mode iterator (error code: %ld)\n", (long)result);
        device->input->Release();
        device->input = nullptr;
        return false;
    }

    IDeckLinkDisplayMode* display_mode = nullptr;
    IDeckLinkDisplayMode* selected_mode = nullptr;
    BMDDisplayMode selected_mode_id = bmdModeUnknown;
    bool found_matching_mode = false;

    // Create a list of available modes for error reporting
    fprintf(stderr, "Available display modes:\n");

    while (display_mode_iterator->Next(&display_mode) == S_OK) {
        BMDTimeValue time_value;
        BMDTimeScale time_scale;

        if (display_mode->GetFrameRate(&time_value, &time_scale) == S_OK) {
            // Calculate the frame rate correctly
            // time_value is the numerator (e.g., 24000)
            // time_scale is the denominator (e.g., 1000)
            float mode_framerate = (float)time_scale / (float)time_value;

            // Log the available mode
            CFStringRef mode_name_ref = NULL;
            display_mode->GetName(&mode_name_ref);

            // Convert CFString to C string
            char mode_name_buffer[128] = "Unknown";
            if (mode_name_ref) {
                CFStringGetCString(mode_name_ref, mode_name_buffer, sizeof(mode_name_buffer), kCFStringEncodingUTF8);
                CFRelease(mode_name_ref);
            }

            fprintf(stderr, "  - %ldx%ld @ %.2f fps (%s)\n",
                    display_mode->GetWidth(), display_mode->GetHeight(),
                    mode_framerate, mode_name_buffer);

            // Store the raw frame rate components for debugging
            fprintf(stderr, "    (raw frame rate: %lld/%lld)\n", (long long)time_value, (long long)time_scale);

            // Check if this mode matches our requested parameters
            // Use a small epsilon for floating point comparison
            const float epsilon = 0.1f;
            bool width_match = display_mode->GetWidth() == width;
            bool height_match = display_mode->GetHeight() == height;

            // Check if frame rates are close enough, accounting for common framerates
            // like 23.98 vs 24, 29.97 vs 30, etc.
            bool framerate_match = false;

            // Common frame rate mappings (requested → actual)
            if (framerate == 24.0f && (fabs(mode_framerate - 24.0f) < epsilon || fabs(mode_framerate - 23.98f) < epsilon)) {
                framerate_match = true;
            }
            else if (framerate == 30.0f && (fabs(mode_framerate - 30.0f) < epsilon || fabs(mode_framerate - 29.97f) < epsilon)) {
                framerate_match = true;
            }
            else if (framerate == 60.0f && (fabs(mode_framerate - 60.0f) < epsilon || fabs(mode_framerate - 59.94f) < epsilon)) {
                framerate_match = true;
            }
            else if (fabs(mode_framerate - framerate) < epsilon) {
                framerate_match = true;
            }

            if (width_match && height_match && framerate_match) {
                fprintf(stderr, "  * Found matching mode: %ldx%ld @ %.2f fps (%s)\n",
                        display_mode->GetWidth(), display_mode->GetHeight(),
                        mode_framerate, mode_name_buffer);
                selected_mode = display_mode;
                selected_mode_id = display_mode->GetDisplayMode();
                found_matching_mode = true;
                continue; // Don't release, we're keeping this one
            }
        }

        display_mode->Release();
    }

    display_mode_iterator->Release();

    if (!found_matching_mode) {
        fprintf(stderr, "Error: No matching display mode found for %dx%d @ %.2f fps\n",
                width, height, framerate);
        device->input->Release();
        device->input = nullptr;
        return false;
    }

    // Enable video input with the selected mode
    BMDVideoInputFlags input_flags = bmdVideoInputFlagDefault;
    BMDPixelFormat pixel_format = bmdFormat8BitYUV;

    result = device->input->EnableVideoInput(selected_mode_id, pixel_format, input_flags);
    if (result != S_OK) {
        fprintf(stderr, "Error: Failed to enable video input (error code: %ld)\n", (long)result);

        // Provide more specific error messages based on common error codes
        switch (result) {
            case E_INVALIDARG:
                fprintf(stderr, "  - Invalid argument (check display mode and pixel format)\n");
                break;
            case E_ACCESSDENIED:
                fprintf(stderr, "  - Access denied (device may be in use by another application)\n");
                break;
            case E_OUTOFMEMORY:
                fprintf(stderr, "  - Out of memory\n");
                break;
            default:
                fprintf(stderr, "  - Hardware error or unsupported configuration\n");
                break;
        }

        selected_mode->Release();
        device->input->Release();
        device->input = nullptr;
        return false;
    }

    selected_mode->Release();

    // Start the stream
    result = device->input->StartStreams();
    if (result != S_OK) {
        fprintf(stderr, "Error: Failed to start capture streams (error code: %ld)\n", (long)result);

        // Provide more specific error messages
        switch (result) {
            case E_ACCESSDENIED:
                fprintf(stderr, "  - Access denied (device may be in use by another application)\n");
                break;
            default:
                fprintf(stderr, "  - Hardware error or device disconnected\n");
                break;
        }

        device->input->DisableVideoInput();
        device->input->Release();
        device->input = nullptr;
        return false;
    }

    device->capturing = true;
    return true;
}

bool bm_update(BMContext* context, BMCaptureDevice* device) {
    if (context == nullptr || device == nullptr || !device->capturing) {
        return false;
    }

    // Swap front buffer to get most recent frame
    return device->buffer.swapFront();
}

bool bm_get_frame(BMContext* context, BMCaptureDevice* device, BMPixelFormat format,
                  uint8_t* buffer, size_t buffer_size,
                  int* out_width, int* out_height, int* out_channels) {
    if (context == nullptr || device == nullptr || buffer == nullptr || !device->capturing) {
        return false;
    }

    CapturedFrame& frame = device->buffer.getFront();

    // Set output dimensions if requested
    if (out_width != nullptr) {
        *out_width = frame.width;
    }

    if (out_height != nullptr) {
        *out_height = frame.height;
    }

    if (out_channels != nullptr) {
        switch (format) {
            case BM_FORMAT_RGB:
                *out_channels = 3;
                break;
            case BM_FORMAT_YUV:
                *out_channels = 2; // 4:2:2 format (2 bytes per pixel)
                break;
            case BM_FORMAT_GRAY:
                *out_channels = 1;
                break;
        }
    }

    // Check if mutex exists and try to lock with timeout
    if (!frame.mutex || !frame.mutex->try_lock_for(std::chrono::milliseconds(device->capture_mode))) {
        return false;
    }

    // Use RAII lock guard for automatic unlocking
    std::lock_guard<std::timed_mutex> lock(*frame.mutex, std::adopt_lock);

    size_t required_size = 0;

    switch (format) {
        case BM_FORMAT_RGB: {
            // Convert YUV to RGB if needed
            if (!frame.rgb_updated && !frame.yuv_data.empty()) {
                unsigned int pixel_count = frame.width * frame.height;
                frame.rgb_data.resize(pixel_count * 3);
                yuv_to_rgb(frame.yuv_data.data(), frame.rgb_data.data(), pixel_count, &device->yuv_tables);
                frame.rgb_updated = true;
            }

            required_size = frame.rgb_data.size();
            if (buffer_size >= required_size) {
                memcpy(buffer, frame.rgb_data.data(), required_size);
                return true;
            }
            break;
        }

        case BM_FORMAT_YUV: {
            required_size = frame.yuv_data.size();
            if (buffer_size >= required_size) {
                memcpy(buffer, frame.yuv_data.data(), required_size);
                return true;
            }
            break;
        }

        case BM_FORMAT_GRAY: {
            // Convert YUV to grayscale if needed
            if (!frame.gray_updated && !frame.yuv_data.empty()) {
                unsigned int pixel_count = frame.width * frame.height;
                frame.gray_data.resize(pixel_count);
                yuv_to_gray(frame.yuv_data.data(), frame.gray_data.data(), pixel_count);
                frame.gray_updated = true;
            }

            required_size = frame.gray_data.size();
            if (buffer_size >= required_size) {
                memcpy(buffer, frame.gray_data.data(), required_size);
                return true;
            }
            break;
        }
    }

    return false;
}

size_t bm_get_frame_size(BMContext* context, BMCaptureDevice* device, BMPixelFormat format) {
    if (context == nullptr || device == nullptr) {
        return 0;
    }

    int width = device->width;
    int height = device->height;

    switch (format) {
        case BM_FORMAT_RGB:
            return width * height * 3;
        case BM_FORMAT_YUV:
            return width * height * 2; // 4:2:2 format is 2 bytes per pixel
        case BM_FORMAT_GRAY:
            return width * height;
        default:
            return 0;
    }
}

void bm_stop_capture(BMContext* context, BMCaptureDevice* device) {
    if (context == nullptr || device == nullptr || !device->capturing) {
        return;
    }

    if (device->input != nullptr) {
        device->input->StopStreams();
        device->input->DisableVideoInput();
        device->input->SetCallback(nullptr);
        device->input->Release();
        device->input = nullptr;
    }

    device->capturing = false;
}

void bm_destroy_device(BMContext* context, BMCaptureDevice* device) {
    fprintf(stderr, "A: %x\n", device);
    if (context == nullptr || device == nullptr) {
        return;
    }
    fprintf(stderr, "B\n");
    // Clean up all channels
    for (auto* channel : device->channels) {
        if (channel->capturing) {
            bm_stop_channel_capture(context, channel);
        }
        delete channel;
    }
    fprintf(stderr, "C\n");
    device->channels.clear();
    fprintf(stderr, "D\n");
    // Clean up the device
    if (device->device != nullptr) {
        device->device->Release();
    }
    fprintf(stderr, "E\n");
    delete device;
    fprintf(stderr, "F\n");
}

// Multi-channel API implementation

int bm_get_channel_count(BMContext* context, BMCaptureDevice* device) {
    if (context == nullptr || device == nullptr || device->device == nullptr) {
        return -1;
    }

    IDeckLinkAttributes* deckLinkAttributes = nullptr;
    int64_t channelCount = 0;

    // Query the device for its capabilities
    if (device->device->QueryInterface(IID_IDeckLinkAttributes, (void**)&deckLinkAttributes) != S_OK) {
        return -1;
    }

    // Get the maximum number of simultaneous inputs
    bool hasAttribute = false;
    deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &hasAttribute);

    // Try to get maximum capture channels attribute if available
    // Note: This is device-specific and might not be available on all cards
    if (deckLinkAttributes->GetInt(BMDDeckLinkMaximumAudioChannels, &channelCount) != S_OK) {
        // If attribute not available, default to 1 for most cards
        channelCount = 1;
    }

    deckLinkAttributes->Release();
    return static_cast<int>(channelCount);
}

BMCaptureChannel* bm_create_channel(BMContext* context, BMCaptureDevice* device, int port_index) {
    if (context == nullptr || device == nullptr || device->device == nullptr) {
        return nullptr;
    }

    // Create the channel object
    BMCaptureChannel* channel = new BMCaptureChannel(device, port_index);

    // Add the channel to the device's channel list
    device->channels.push_back(channel);

    return channel;
}

bool bm_start_channel_capture(BMContext* context, BMCaptureChannel* channel,
                             int width, int height, float framerate, BMCaptureMode mode) {
    if (context == nullptr || channel == nullptr || channel->parent_device == nullptr ||
        channel->parent_device->device == nullptr) {
        fprintf(stderr, "Error: Invalid channel handle\n");
        return false;
    }

    if (channel->capturing) {
        bm_stop_channel_capture(context, channel);
    }

    channel->width = width;
    channel->height = height;
    channel->capture_mode = mode;

    // Get the IDeckLinkInput interface
    HRESULT result = channel->parent_device->device->QueryInterface(IID_IDeckLinkInput, (void**)&channel->input);
    if (result != S_OK) {
        fprintf(stderr, "Error: Failed to get DeckLink input interface (error code: %ld)\n", (long)result);
        return false;
    }

    // Set the callback
    channel->input->SetCallback(channel->callback);

    // Select input port
    IDeckLinkConfiguration* deckLinkConfig = nullptr;
    if (channel->parent_device->device->QueryInterface(IID_IDeckLinkConfiguration, (void**)&deckLinkConfig) == S_OK) {
        // Get available input connections
        IDeckLinkAttributes* deckLinkAttributes = nullptr;
        if (channel->parent_device->device->QueryInterface(IID_IDeckLinkAttributes, (void**)&deckLinkAttributes) == S_OK) {
            int64_t connectionsValue = 0;
            if (deckLinkAttributes->GetInt(BMDDeckLinkVideoInputConnections, &connectionsValue) == S_OK) {
                BMDVideoConnection connections = (BMDVideoConnection)connectionsValue;

                // Create a vector of port connections
                std::vector<BMDVideoConnection> portConnections;
                if (connections & bmdVideoConnectionSDI)
                    portConnections.push_back(bmdVideoConnectionSDI);
                if (connections & bmdVideoConnectionHDMI)
                    portConnections.push_back(bmdVideoConnectionHDMI);
                if (connections & bmdVideoConnectionOpticalSDI)
                    portConnections.push_back(bmdVideoConnectionOpticalSDI);
                if (connections & bmdVideoConnectionComponent)
                    portConnections.push_back(bmdVideoConnectionComponent);
                if (connections & bmdVideoConnectionComposite)
                    portConnections.push_back(bmdVideoConnectionComposite);
                if (connections & bmdVideoConnectionSVideo)
                    portConnections.push_back(bmdVideoConnectionSVideo);

                // Get the connection for the requested port
                if (channel->port_index >= 0 && channel->port_index < (int)portConnections.size()) {
                    BMDVideoConnection selectedConnection = portConnections[channel->port_index];
                    deckLinkConfig->SetInt(bmdDeckLinkConfigVideoInputConnection, selectedConnection);
                }
            }
            deckLinkAttributes->Release();
        }
        deckLinkConfig->Release();
    }

    // Find the appropriate display mode
    IDeckLinkDisplayModeIterator* display_mode_iterator = nullptr;
    result = channel->input->GetDisplayModeIterator(&display_mode_iterator);
    if (result != S_OK) {
        fprintf(stderr, "Error: Failed to get display mode iterator (error code: %ld)\n", (long)result);
        channel->input->Release();
        channel->input = nullptr;
        return false;
    }

    IDeckLinkDisplayMode* display_mode = nullptr;
    IDeckLinkDisplayMode* selected_mode = nullptr;
    BMDDisplayMode selected_mode_id = bmdModeUnknown;
    bool found_matching_mode = false;

    // Skip listing all modes to reduce verbosity
    while (display_mode_iterator->Next(&display_mode) == S_OK) {
        BMDTimeValue time_value;
        BMDTimeScale time_scale;

        if (display_mode->GetFrameRate(&time_value, &time_scale) == S_OK) {
            // Calculate the frame rate correctly
            // time_value is the numerator (e.g., 24000)
            // time_scale is the denominator (e.g., 1000)
            float mode_framerate = (float)time_scale / (float)time_value;

            // Get mode name but don't print it
            CFStringRef mode_name_ref = NULL;
            display_mode->GetName(&mode_name_ref);

            // Convert CFString to C string
            char mode_name_buffer[128] = "Unknown";
            if (mode_name_ref) {
                CFStringGetCString(mode_name_ref, mode_name_buffer, sizeof(mode_name_buffer), kCFStringEncodingUTF8);
                CFRelease(mode_name_ref);
            }

            // Check if this mode matches our requested parameters
            // Use a small epsilon for floating point comparison
            const float epsilon = 0.1f;
            bool width_match = display_mode->GetWidth() == width;
            bool height_match = display_mode->GetHeight() == height;

            // Check if frame rates are close enough, accounting for common framerates
            // like 23.98 vs 24, 29.97 vs 30, etc.
            bool framerate_match = false;

            // Common frame rate mappings (requested → actual)
            if (framerate == 24.0f && (fabs(mode_framerate - 24.0f) < epsilon || fabs(mode_framerate - 23.98f) < epsilon)) {
                framerate_match = true;
            }
            else if (framerate == 30.0f && (fabs(mode_framerate - 30.0f) < epsilon || fabs(mode_framerate - 29.97f) < epsilon)) {
                framerate_match = true;
            }
            else if (framerate == 60.0f && (fabs(mode_framerate - 60.0f) < epsilon || fabs(mode_framerate - 59.94f) < epsilon)) {
                framerate_match = true;
            }
            else if (fabs(mode_framerate - framerate) < epsilon) {
                framerate_match = true;
            }

            if (width_match && height_match && framerate_match) {
                fprintf(stderr, "  * Found matching mode: %ldx%ld @ %.2f fps (%s)\n",
                        display_mode->GetWidth(), display_mode->GetHeight(),
                        mode_framerate, mode_name_buffer);
                selected_mode = display_mode;
                selected_mode_id = display_mode->GetDisplayMode();
                found_matching_mode = true;
                continue; // Don't release, we're keeping this one
            }
        }

        display_mode->Release();
    }

    display_mode_iterator->Release();

    if (!found_matching_mode) {
        fprintf(stderr, "Error: No matching display mode found for %dx%d @ %.2f fps\n",
                width, height, framerate);
        channel->input->Release();
        channel->input = nullptr;
        return false;
    }

    // Enable video input with the selected mode
    BMDVideoInputFlags input_flags = bmdVideoInputFlagDefault;
    BMDPixelFormat pixel_format = bmdFormat8BitYUV;

    result = channel->input->EnableVideoInput(selected_mode_id, pixel_format, input_flags);
    if (result != S_OK) {
        fprintf(stderr, "Error: Failed to enable video input (error code: %ld)\n", (long)result);

        // Provide more specific error messages based on common error codes
        switch (result) {
            case E_INVALIDARG:
                fprintf(stderr, "  - Invalid argument (check display mode and pixel format)\n");
                break;
            case E_ACCESSDENIED:
                fprintf(stderr, "  - Access denied (device may be in use by another application)\n");
                break;
            case E_OUTOFMEMORY:
                fprintf(stderr, "  - Out of memory\n");
                break;
            default:
                fprintf(stderr, "  - Hardware error or unsupported configuration\n");
                break;
        }

        selected_mode->Release();
        channel->input->Release();
        channel->input = nullptr;
        return false;
    }

    selected_mode->Release();

    // Start the stream
    result = channel->input->StartStreams();
    if (result != S_OK) {
        fprintf(stderr, "Error: Failed to start capture streams (error code: %ld)\n", (long)result);

        // Provide more specific error messages
        switch (result) {
            case E_ACCESSDENIED:
                fprintf(stderr, "  - Access denied (device may be in use by another application)\n");
                break;
            default:
                fprintf(stderr, "  - Hardware error or device disconnected\n");
                break;
        }

        channel->input->DisableVideoInput();
        channel->input->Release();
        channel->input = nullptr;
        return false;
    }

    channel->capturing = true;
    return true;
}

bool bm_update_channel(BMContext* context, BMCaptureChannel* channel) {
    if (context == nullptr || channel == nullptr || !channel->capturing) {
        return false;
    }

    // Check if we have received any frames yet
    if (channel->frame_count == 0) {
        return false;
    }

    // In the first few frames, we've already primed the buffer, so just return true
    if (channel->frame_count <= channel->min_frames_for_lock) {
        return true;
    }

    // Swap front buffer to get most recent frame
    return channel->buffer.swapFront();
}

bool bm_channel_has_valid_signal(BMContext* context, BMCaptureChannel* channel) {
    if (context == nullptr || channel == nullptr || !channel->capturing) {
        return false;
    }

    return channel->hasValidSignal();
}

bool bm_channel_has_stable_frame_rate(BMContext* context, BMCaptureChannel* channel) {
    if (context == nullptr || channel == nullptr || !channel->capturing) {
        return false;
    }

    return channel->isFrameRateStable();
}

int bm_channel_get_frame_count(BMContext* context, BMCaptureChannel* channel) {
    if (context == nullptr || channel == nullptr || !channel->capturing) {
        return 0;
    }

    return channel->frame_count;
}

bool bm_channel_set_signal_parameters(BMContext* context, BMCaptureChannel* channel,
                                     int min_frames, int max_bad_frames) {
    if (context == nullptr || channel == nullptr || !channel->capturing) {
        return false;
    }

    // Validate parameters
    if (min_frames < 1 || max_bad_frames < 1) {
        return false;
    }

    // Update channel parameters
    channel->min_frames_for_lock = min_frames;
    channel->max_lost_frames = max_bad_frames;

    return true;
}

bool bm_get_channel_frame(BMContext* context, BMCaptureChannel* channel, BMPixelFormat format,
                         uint8_t* buffer, size_t buffer_size,
                         int* out_width, int* out_height, int* out_channels) {
    if (context == nullptr || channel == nullptr || buffer == nullptr || !channel->capturing) {
        return false;
    }

    CapturedFrame& frame = channel->buffer.getFront();

    // Set output dimensions if requested
    if (out_width != nullptr) {
        *out_width = frame.width;
    }

    if (out_height != nullptr) {
        *out_height = frame.height;
    }

    if (out_channels != nullptr) {
        switch (format) {
            case BM_FORMAT_RGB:
                *out_channels = 3;
                break;
            case BM_FORMAT_YUV:
                *out_channels = 2; // 4:2:2 format (2 bytes per pixel)
                break;
            case BM_FORMAT_GRAY:
                *out_channels = 1;
                break;
        }
    }

    // Check if mutex exists and try to lock with timeout
    if (!frame.mutex || !frame.mutex->try_lock_for(std::chrono::milliseconds(channel->capture_mode))) {
        return false;
    }

    // Use RAII lock guard for automatic unlocking
    std::lock_guard<std::timed_mutex> lock(*frame.mutex, std::adopt_lock);

    size_t required_size = 0;

    switch (format) {
        case BM_FORMAT_RGB: {
            // Convert YUV to RGB if needed
            if (!frame.rgb_updated && !frame.yuv_data.empty()) {
                unsigned int pixel_count = frame.width * frame.height;
                frame.rgb_data.resize(pixel_count * 3);
                yuv_to_rgb(frame.yuv_data.data(), frame.rgb_data.data(), pixel_count, &channel->yuv_tables);
                frame.rgb_updated = true;
            }

            required_size = frame.rgb_data.size();
            if (buffer_size >= required_size) {
                memcpy(buffer, frame.rgb_data.data(), required_size);
                return true;
            }
            break;
        }

        case BM_FORMAT_YUV: {
            required_size = frame.yuv_data.size();
            if (buffer_size >= required_size) {
                memcpy(buffer, frame.yuv_data.data(), required_size);
                return true;
            }
            break;
        }

        case BM_FORMAT_GRAY: {
            // Convert YUV to grayscale if needed
            if (!frame.gray_updated && !frame.yuv_data.empty()) {
                unsigned int pixel_count = frame.width * frame.height;
                frame.gray_data.resize(pixel_count);
                yuv_to_gray(frame.yuv_data.data(), frame.gray_data.data(), pixel_count);
                frame.gray_updated = true;
            }

            required_size = frame.gray_data.size();
            if (buffer_size >= required_size) {
                memcpy(buffer, frame.gray_data.data(), required_size);
                return true;
            }
            break;
        }
    }

    return false;
}

size_t bm_get_channel_frame_size(BMContext* context, BMCaptureChannel* channel, BMPixelFormat format) {
    if (context == nullptr || channel == nullptr) {
        return 0;
    }

    int width = channel->width;
    int height = channel->height;

    switch (format) {
        case BM_FORMAT_RGB:
            return width * height * 3;
        case BM_FORMAT_YUV:
            return width * height * 2; // 4:2:2 format is 2 bytes per pixel
        case BM_FORMAT_GRAY:
            return width * height;
        default:
            return 0;
    }
}

void bm_stop_channel_capture(BMContext* context, BMCaptureChannel* channel) {
    if (context == nullptr || channel == nullptr || !channel->capturing) {
        return;
    }

    if (channel->input != nullptr) {
        channel->input->StopStreams();
        channel->input->DisableVideoInput();
        channel->input->SetCallback(nullptr);
        channel->input->Release();
        channel->input = nullptr;
    }

    channel->capturing = false;
}

void bm_destroy_channel(BMContext* context, BMCaptureChannel* channel) {
    if (context == nullptr || channel == nullptr) {
        return;
    }

    if (channel->capturing) {
        bm_stop_channel_capture(context, channel);
    }

    // Remove the channel from its parent device's list
    if (channel->parent_device) {
        auto& channels = channel->parent_device->channels;
        channels.erase(std::remove(channels.begin(), channels.end(), channel), channels.end());
    }

    delete channel;
}
