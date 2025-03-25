#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <numpy/arrayobject.h>

#include "../bmcapture.h"

// Global context for the library
static BMContext* g_context = NULL;


// Struct for the Python BMCapture object
typedef struct {
    PyObject_HEAD
    BMCaptureDevice* device;
    BMCaptureChannel* channel;  // Primary channel for backward compatibility
    int width;
    int height;
} BMCaptureObject;

// Struct for the Python BMChannel object
typedef struct {
    PyObject_HEAD
    BMCaptureChannel* channel;
    int width;
    int height;
} BMChannelObject;

//Forward Declare functions for reference in static structs.
static PyObject* BMChannel_new(PyTypeObject* type, PyObject* args, PyObject* kwds);
static int BMCapture_init(BMCaptureObject* self, PyObject* args, PyObject* kwds);
static void BMChannel_dealloc(BMChannelObject* self);
static PyObject* BMChannel_has_valid_signal(BMChannelObject* self, PyObject* args);
static PyObject* BMChannel_has_stable_frame_rate(BMChannelObject* self, PyObject* args);

static PyObject* BMChannel_get_frame_count(BMChannelObject* self, PyObject* args);
static PyObject* BMChannel_set_signal_parameters(BMChannelObject* self, PyObject* args, PyObject* kwds);
static PyObject* BMChannel_update(BMChannelObject* self, PyObject* args);
static PyObject* BMChannel_has_valid_signal(BMChannelObject* self, PyObject* args);
static PyObject* BMChannel_close(BMChannelObject* self, PyObject* args);
static int BMChannel_init(BMChannelObject* self, PyObject* args, PyObject* kwds);
static PyObject* BMChannel_get_frame(BMChannelObject* self, PyObject* args, PyObject* kwds);

static PyObject* BMCapture_create_channel(BMCaptureObject* self, PyObject* args, PyObject* kwds);
static PyObject* BMCapture_new(PyTypeObject* type, PyObject* args, PyObject* kwds);
static void BMCapture_dealloc(BMCaptureObject* self);
static PyObject* BMCapture_update(BMCaptureObject* self, PyObject* args);
static PyObject* BMCapture_get_frame(BMCaptureObject* self, PyObject* args, PyObject* kwds);
static PyObject* BMCapture_get_channel_count(BMCaptureObject* self, PyObject* args);
static PyObject* BMCapture_create_channel(BMCaptureObject* self, PyObject* args, PyObject* kwds);
static PyObject* BMCapture_close(BMCaptureObject* self, PyObject* args);

// Method definitions for BMCapture
static PyMethodDef BMCapture_methods[] = {
    {"update", (PyCFunction)BMCapture_update, METH_NOARGS,
     "Check for new frames. Returns True if a new frame is available."},
    {"get_frame", (PyCFunction)BMCapture_get_frame, METH_VARARGS | METH_KEYWORDS,
     "Get the latest frame as a NumPy array. Format can be 'rgb', 'yuv', or 'gray'."},
    {"get_channel_count", (PyCFunction)BMCapture_get_channel_count, METH_NOARGS,
     "Get the number of channels supported by this device."},
    {"create_channel", (PyCFunction)BMCapture_create_channel, METH_VARARGS | METH_KEYWORDS,
     "Create a new channel on this device."},
    {"has_valid_signal", (PyCFunction)BMChannel_has_valid_signal, METH_NOARGS,
     "Check if the device has a valid signal lock with stable frames."},
    {"has_stable_frame_rate", (PyCFunction)BMChannel_has_stable_frame_rate, METH_NOARGS,
     "Check if frames are being received at a consistent rate."},
    {"get_frame_count", (PyCFunction)BMChannel_get_frame_count, METH_NOARGS,
     "Get the number of frames received since starting capture."},
    {"set_signal_parameters", (PyCFunction)BMChannel_set_signal_parameters, METH_VARARGS | METH_KEYWORDS,
     "Set parameters for signal detection: min_frames (default 3), max_bad_frames (default 5)."},
    {"close", (PyCFunction)BMCapture_close, METH_NOARGS,
     "Close the device and release resources."},
    {NULL}  /* Sentinel */
};


// Type definition for BMCapture
static PyTypeObject BMCaptureType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "bmcapture.BMCapture",
    .tp_doc = "BlackMagic Capture Device",
    .tp_basicsize = sizeof(BMCaptureObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = BMCapture_new,
    .tp_init = (initproc)BMCapture_init,
    .tp_dealloc = (destructor)BMCapture_dealloc,
    .tp_methods = BMCapture_methods,
};


// Method definitions for BMChannel
static PyMethodDef BMChannel_methods[] = {
    {"update", (PyCFunction)BMChannel_update, METH_NOARGS,
     "Check for new frames. Returns True if a new frame is available."},
    {"get_frame", (PyCFunction)BMChannel_get_frame, METH_VARARGS | METH_KEYWORDS,
     "Get the latest frame as a NumPy array. Format can be 'rgb', 'yuv', or 'gray'."},
    {"has_valid_signal", (PyCFunction)BMChannel_has_valid_signal, METH_NOARGS,
     "Check if the channel has a valid signal lock with stable frames."},
    {"has_stable_frame_rate", (PyCFunction)BMChannel_has_stable_frame_rate, METH_NOARGS,
     "Check if frames are being received at a consistent rate."},
    {"get_frame_count", (PyCFunction)BMChannel_get_frame_count, METH_NOARGS,
     "Get the number of frames received since starting capture."},
    {"set_signal_parameters", (PyCFunction)BMChannel_set_signal_parameters, METH_VARARGS | METH_KEYWORDS,
     "Set parameters for signal detection: min_frames (default 3), max_bad_frames (default 5)."},
    {"close", (PyCFunction)BMChannel_close, METH_NOARGS,
     "Close the channel and release resources."},
    {NULL}  /* Sentinel */
};

// Type definition for BMChannel already forward declared
// Initialize it here
static PyTypeObject BMChannelType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "bmcapture.BMChannel",
    .tp_doc = "BlackMagic Capture Channel",
    .tp_basicsize = sizeof(BMChannelObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = BMChannel_new,
    .tp_init = (initproc)BMChannel_init,
    .tp_dealloc = (destructor)BMChannel_dealloc,
    .tp_methods = BMChannel_methods,
};

// Deallocation function for BMCapture
static void BMCapture_dealloc(BMCaptureObject* self) {
    if (g_context) {
        // The channel is managed by the device, so we don't destroy it separately
        if (self->device) {
            bm_stop_capture(g_context, self->device);
            bm_destroy_device(g_context, self->device);
            self->device = NULL;
            self->channel = NULL;  // Channel is managed by the device
        }
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

// Deallocation function for BMChannel
static void BMChannel_dealloc(BMChannelObject* self) {
    if (g_context && self->channel) {
        bm_stop_channel_capture(g_context, self->channel);
        bm_destroy_channel(g_context, self->channel);
        self->channel = NULL;
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

// Initialization function for BMCapture
static PyObject* BMCapture_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    BMCaptureObject* self;
    self = (BMCaptureObject*)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->device = NULL;
        self->channel = NULL;
        self->width = 0;
        self->height = 0;
    }
    return (PyObject*)self;
}

// Initialization function for BMChannel
static PyObject* BMChannel_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    BMChannelObject* self;
    self = (BMChannelObject*)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->channel = NULL;
        self->width = 0;
        self->height = 0;
    }
    return (PyObject*)self;
}

// Initialize the capture device
static int BMCapture_init(BMCaptureObject* self, PyObject* args, PyObject* kwds) {
    static const char* const_kwlist[] = {"device_index", "width", "height", "framerate", "low_latency", "port_index", NULL};
    static char** kwlist = const_cast<char**>(const_kwlist);

    int device_index = 0;
    int width = 1920;
    int height = 1080;
    float framerate = 30.0f;
    int low_latency = 1; // Default to low latency
    int port_index = 0;  // Default to first port

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|iiifpi", kwlist,
                                     &device_index, &width, &height, &framerate, &low_latency, &port_index)) {
        return -1;
    }

    // Initialize context if needed
    if (g_context == NULL) {
        g_context = bm_create_context();
        if (g_context == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create BlackMagic context");
            return -1;
        }
    }

    // Create device
    self->device = bm_create_device(g_context, device_index);
    if (self->device == NULL) {
        PyErr_Format(PyExc_RuntimeError, "Failed to create device with index %d", device_index);
        return -1;
    }

    // Create a primary channel for backward compatibility
    self->channel = bm_create_channel(g_context, self->device, port_index);
    if (self->channel == NULL) {
        bm_destroy_device(g_context, self->device);
        self->device = NULL;
        PyErr_Format(PyExc_RuntimeError, "Failed to create channel on port %d", port_index);
        return -1;
    }

    // Start capture on the channel
    BMCaptureMode mode = low_latency ? BM_LOW_LATENCY : BM_NO_FRAME_DROPS;
    if (!bm_start_channel_capture(g_context, self->channel, width, height, framerate, mode)) {
        // Channel is cleaned up by the device when it's destroyed
        bm_destroy_device(g_context, self->device);
        self->device = NULL;
        self->channel = NULL;
        PyErr_Format(PyExc_RuntimeError,
                    "Failed to start capture with settings: %dx%d @ %0.2f fps on port %d",
                    width, height, framerate, port_index);
        return -1;
    }

    self->width = width;
    self->height = height;

    return 0;
}

// Initialize a channel
static int BMChannel_init(BMChannelObject* self, PyObject* args, PyObject* kwds) {
    static const char* const_kwlist[] = {"device", "port_index", "width", "height", "framerate", "low_latency", NULL};
    static char** kwlist = const_cast<char**>(const_kwlist);

    PyObject* device_obj;
    int port_index = 0;
    int width = 1920;
    int height = 1080;
    float framerate = 30.0f;
    int low_latency = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Oi|iifp", kwlist,
                                     &device_obj, &port_index, &width, &height, &framerate, &low_latency)) {
        return -1;
    }

    // Check if device is a BMCapture object
    if (!PyObject_IsInstance(device_obj, (PyObject*)&BMCaptureType)) {
        PyErr_SetString(PyExc_TypeError, "First argument must be a BMCapture device");
        return -1;
    }

    BMCaptureObject* device_self = (BMCaptureObject*)device_obj;

    // Check if the device is valid
    if (!device_self->device) {
        PyErr_SetString(PyExc_RuntimeError, "Device has been closed or is invalid");
        return -1;
    }

    // Initialize context if needed
    if (g_context == NULL) {
        g_context = bm_create_context();
        if (g_context == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create BlackMagic context");
            return -1;
        }
    }

    // Create the channel
    self->channel = bm_create_channel(g_context, device_self->device, port_index);
    if (self->channel == NULL) {
        PyErr_Format(PyExc_RuntimeError, "Failed to create channel on port %d", port_index);
        return -1;
    }

    // Start capture on the channel
    BMCaptureMode mode = low_latency ? BM_LOW_LATENCY : BM_NO_FRAME_DROPS;
    if (!bm_start_channel_capture(g_context, self->channel, width, height, framerate, mode)) {
        bm_destroy_channel(g_context, self->channel);
        self->channel = NULL;
        PyErr_Format(PyExc_RuntimeError,
                    "Failed to start capture with settings: %dx%d @ %0.2f fps on port %d",
                    width, height, framerate, port_index);
        return -1;
    }

    self->width = width;
    self->height = height;

    return 0;
}

// Initialize the library
static PyObject* BMCapture_initialize(PyObject* self, PyObject* args) {
    if (g_context == NULL) {
        g_context = bm_create_context();
        if (g_context == NULL) {
            Py_RETURN_FALSE;
        }
    }
    Py_RETURN_TRUE;
}

// Shutdown the library - safely clean up resources
static PyObject* BMCapture_shutdown(PyObject* self, PyObject* args) {
    if (g_context != NULL) {
        // First, ensure we stop all active capture operations
        // This is important to prevent crashes during interpreter shutdown
        // Note: In a real-world implementation, you might need to track all active
        // devices/channels and explicitly close them here
        
        // Now free the context
        bm_free_context(g_context);
        g_context = NULL;
    }
    Py_RETURN_NONE;
}

// Get number of devices
static PyObject* BMCapture_get_device_count(PyObject* self, PyObject* args) {
    // Initialize context if needed
    if (g_context == NULL) {
        g_context = bm_create_context();
        if (g_context == NULL) {
            return PyLong_FromLong(0);
        }
    }

    int count = bm_get_device_count(g_context);
    return PyLong_FromLong(count);
}

// Get a device name by index
static PyObject* BMCapture_get_device_name(PyObject* self, PyObject* args) {
    int device_index;

    if (!PyArg_ParseTuple(args, "i", &device_index)) {
        return NULL;
    }

    // Initialize context if needed
    if (g_context == NULL) {
        g_context = bm_create_context();
        if (g_context == NULL) {
            Py_RETURN_NONE;
        }
    }

    char name[256];
    if (bm_get_device_name(g_context, device_index, name, sizeof(name))) {
        return PyUnicode_FromString(name);
    } else {
        Py_RETURN_NONE;
    }
}

// Get a list of available devices
static PyObject* BMCapture_get_devices(PyObject* self, PyObject* args) {
    // Initialize context if needed
    if (g_context == NULL) {
        g_context = bm_create_context();
        if (g_context == NULL) {
            return PyList_New(0);
        }
    }

    int count = bm_get_device_count(g_context);
    PyObject* device_list = PyList_New(count);

    for (int i = 0; i < count; i++) {
        char name[256];
        if (bm_get_device_name(g_context, i, name, sizeof(name))) {
            PyList_SetItem(device_list, i, PyUnicode_FromString(name));
        } else {
            PyList_SetItem(device_list, i, PyUnicode_FromFormat("Device %d", i));
        }
    }

    return device_list;
}

// Get input ports for a device
static PyObject* BMCapture_get_input_ports(PyObject* self, PyObject* args) {
    int device_index;

    if (!PyArg_ParseTuple(args, "i", &device_index)) {
        return NULL;
    }

    // Initialize context if needed
    if (g_context == NULL) {
        g_context = bm_create_context();
        if (g_context == NULL) {
            return PyList_New(0);
        }
    }

    int count = bm_get_input_port_count(g_context, device_index);
    if (count <= 0) {
        // Return empty list if no ports
        return PyList_New(0);
    }

    PyObject* port_list = PyList_New(count);

    for (int i = 0; i < count; i++) {
        char name[256];
        if (bm_get_input_port_name(g_context, device_index, i, name, sizeof(name))) {
            PyList_SetItem(port_list, i, PyUnicode_FromString(name));
        } else {
            PyList_SetItem(port_list, i, PyUnicode_FromFormat("Port %d", i));
        }
    }

    return port_list;
}

// Create a device instance
static PyObject* BMCapture_create_device(PyObject* self, PyObject* args) {
    int device_index;

    if (!PyArg_ParseTuple(args, "i", &device_index)) {
        return NULL;
    }

    // Initialize context if needed
    if (g_context == NULL) {
        g_context = bm_create_context();
        if (g_context == NULL) {
            Py_RETURN_NONE;
        }
    }

    BMCaptureDevice* device = bm_create_device(g_context, device_index);
    if (!device) {
        Py_RETURN_NONE;
    }

    // Return the device as a PyCapsule
    return PyCapsule_New(device, "BMCaptureDevice", NULL);
}

// Select an input port
static PyObject* BMCapture_select_input_port(PyObject* self, PyObject* args) {
    PyObject* capsule;
    int port_index;

    if (!PyArg_ParseTuple(args, "Oi", &capsule, &port_index)) {
        return NULL;
    }

    // Initialize context if needed
    if (g_context == NULL) {
        g_context = bm_create_context();
        if (g_context == NULL) {
            Py_RETURN_FALSE;
        }
    }

    // Extract device from capsule
    if (!PyCapsule_IsValid(capsule, "BMCaptureDevice")) {
        PyErr_SetString(PyExc_TypeError, "Invalid device handle");
        return NULL;
    }

    BMCaptureDevice* device = (BMCaptureDevice*)PyCapsule_GetPointer(capsule, "BMCaptureDevice");
    bool success = bm_select_input_port(g_context, device, port_index);

    return PyBool_FromLong(success ? 1 : 0);
}

// Destroy a device instance
static PyObject* BMCapture_destroy_device(PyObject* self, PyObject* args) {
    PyObject* capsule;

    if (!PyArg_ParseTuple(args, "O", &capsule)) {
        return NULL;
    }

    // Check if we have a valid context
    if (g_context == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "BlackMagic context not initialized");
        return NULL;
    }

    // Extract device from capsule
    if (!PyCapsule_IsValid(capsule, "BMCaptureDevice")) {
        PyErr_SetString(PyExc_TypeError, "Invalid device handle");
        return NULL;
    }

    BMCaptureDevice* device = (BMCaptureDevice*)PyCapsule_GetPointer(capsule, "BMCaptureDevice");
    bm_destroy_device(g_context, device);

    // Invalidate the capsule
    PyCapsule_SetPointer(capsule, NULL);

    Py_RETURN_NONE;
}

// Update method - check for new frames
static PyObject* BMCapture_update(BMCaptureObject* self, PyObject* args) {
    if (!self->device || !self->channel) {
        PyErr_SetString(PyExc_RuntimeError, "Device not initialized or has been closed");
        return NULL;
    }

    if (g_context == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "BlackMagic context not initialized");
        return NULL;
    }

    bool new_frame = bm_update_channel(g_context, self->channel);
    return PyBool_FromLong(new_frame ? 1 : 0);
}

// Get the latest frame as a NumPy array
static PyObject* BMCapture_get_frame(BMCaptureObject* self, PyObject* args, PyObject* kwds) {
    static const char* const_kwlist[] = {"format", NULL};
    static char** kwlist = const_cast<char**>(const_kwlist);
    const char* format_str = "rgb";

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &format_str)) {
        return NULL;
    }

    if (!self->device || !self->channel) {
        PyErr_SetString(PyExc_RuntimeError, "Device not initialized or has been closed");
        return NULL;
    }

    if (g_context == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "BlackMagic context not initialized");
        return NULL;
    }

    // Determine format
    BMPixelFormat format;
    int channels;

    if (strcmp(format_str, "rgb") == 0) {
        format = BM_FORMAT_RGB;
        channels = 3;
    } else if (strcmp(format_str, "yuv") == 0) {
        format = BM_FORMAT_YUV;
        channels = 2;  // 4:2:2 format, 2 bytes per pixel
    } else if (strcmp(format_str, "gray") == 0 || strcmp(format_str, "grey") == 0) {
        format = BM_FORMAT_GRAY;
        channels = 1;
    } else {
        PyErr_SetString(PyExc_ValueError, "Invalid format. Must be 'rgb', 'yuv', or 'gray'");
        return NULL;
    }

    // Get dimensions
    int width, height;
    size_t buffer_size = bm_get_channel_frame_size(g_context, self->channel, format);

    if (buffer_size == 0) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to determine frame size");
        return NULL;
    }

    // Create NumPy array
    npy_intp dims[3];
    if (format == BM_FORMAT_YUV) {
        // YUV is a special case - it's 4:2:2 format, so width is halved for array shape
        width = self->width / 2;
        height = self->height;
        dims[0] = height;
        dims[1] = width;
        dims[2] = 4;  // 4 bytes per 2 pixels (cb-y0-cr-y1)
    } else {
        width = self->width;
        height = self->height;
        dims[0] = height;
        dims[1] = width;
        if (channels > 1) {
            dims[2] = channels;
        }
    }

    // Create the NumPy array
    PyObject* array;
    if (channels == 1) {
        array = PyArray_SimpleNew(2, dims, NPY_UINT8);
    } else {
        array = PyArray_SimpleNew(3, dims, NPY_UINT8);
    }

    if (!array) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate NumPy array");
        return NULL;
    }

    // Get frame data into the NumPy array
    uint8_t* buffer = (uint8_t*)PyArray_DATA((PyArrayObject*)array);

    if (!bm_get_channel_frame(g_context, self->channel, format, buffer, buffer_size, &width, &height, NULL)) {
        Py_DECREF(array);
        PyErr_SetString(PyExc_RuntimeError, "Failed to get frame data");
        return NULL;
    }

    return array;
}

// Get the number of channels supported by the device
static PyObject* BMCapture_get_channel_count(BMCaptureObject* self, PyObject* args) {
    if (!self->device) {
        PyErr_SetString(PyExc_RuntimeError, "Device not initialized or has been closed");
        return NULL;
    }

    if (g_context == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "BlackMagic context not initialized");
        return NULL;
    }

    int count = bm_get_channel_count(g_context, self->device);
    return PyLong_FromLong(count);
}

// Create a new channel on the device
static PyObject* BMCapture_create_channel(BMCaptureObject* self, PyObject* args, PyObject* kwds) {
    static const char* const_kwlist[] = {"port_index", "width", "height", "framerate", "low_latency", NULL};
    static char** kwlist = const_cast<char**>(const_kwlist);

    int port_index = 0;
    int width = 1920;
    int height = 1080;
    float framerate = 30.0f;
    int low_latency = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|iifp", kwlist,
                                    &port_index, &width, &height, &framerate, &low_latency)) {
        return NULL;
    }
    
    if (!self->device) {
        PyErr_SetString(PyExc_RuntimeError, "Device not initialized or has been closed");
        return NULL;
    }

    // Create a new BMChannel Python object
    PyObject* channel_type = (PyObject*)&BMChannelType;
    PyObject* arglist = Py_BuildValue("Oi", self, port_index);
    PyObject* kwargdict = Py_BuildValue("{s:i,s:i,s:f,s:O}",
                                       "width", width,
                                       "height", height,
                                       "framerate", framerate,
                                       "low_latency", low_latency ? Py_True : Py_False);

    PyObject* channel_obj = PyObject_Call(channel_type, arglist, kwargdict);

    Py_DECREF(arglist);
    Py_DECREF(kwargdict);

    return channel_obj;
}

// Close the device and release resources
static PyObject* BMCapture_close(BMCaptureObject* self, PyObject* args) {
    if (self->device && g_context) {
        bm_stop_capture(g_context, self->device);
        bm_destroy_device(g_context, self->device);
        self->device = NULL;
        self->channel = NULL;  // Channel is managed by the device
    }

    Py_RETURN_NONE;
}

// Channel methods

// Update method for channel - check for new frames
static PyObject* BMChannel_update(BMChannelObject* self, PyObject* args) {
    if (!self->channel) {
        PyErr_SetString(PyExc_RuntimeError, "Channel not initialized or has been closed");
        return NULL;
    }

    if (g_context == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "BlackMagic context not initialized");
        return NULL;
    }

    bool new_frame = bm_update_channel(g_context, self->channel);
    return PyBool_FromLong(new_frame ? 1 : 0);
}

// Check if channel has a valid signal
static PyObject* BMChannel_has_valid_signal(BMChannelObject* self, PyObject* args) {
    if (!self->channel) {
        PyErr_SetString(PyExc_RuntimeError, "Channel not initialized or has been closed");
        return NULL;
    }

    if (g_context == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "BlackMagic context not initialized");
        return NULL;
    }

    bool has_signal = bm_channel_has_valid_signal(g_context, self->channel);
    return PyBool_FromLong(has_signal ? 1 : 0);
}

// Check if channel has stable frame rate
static PyObject* BMChannel_has_stable_frame_rate(BMChannelObject* self, PyObject* args) {
    if (!self->channel) {
        PyErr_SetString(PyExc_RuntimeError, "Channel not initialized or has been closed");
        return NULL;
    }

    if (g_context == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "BlackMagic context not initialized");
        return NULL;
    }

    bool is_stable = bm_channel_has_stable_frame_rate(g_context, self->channel);
    return PyBool_FromLong(is_stable ? 1 : 0);
}

// Get frame count
static PyObject* BMChannel_get_frame_count(BMChannelObject* self, PyObject* args) {
    if (!self->channel) {
        PyErr_SetString(PyExc_RuntimeError, "Channel not initialized or has been closed");
        return NULL;
    }

    if (g_context == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "BlackMagic context not initialized");
        return NULL;
    }

    int count = bm_channel_get_frame_count(g_context, self->channel);
    return PyLong_FromLong(count);
}

// Set signal parameters
static PyObject* BMChannel_set_signal_parameters(BMChannelObject* self, PyObject* args, PyObject* kwds) {
    static const char* const_kwlist[] = {"min_frames", "max_bad_frames", NULL};
    static char** kwlist = const_cast<char**>(const_kwlist);

    int min_frames = 3;
    int max_bad_frames = 5;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ii", kwlist,
                                    &min_frames, &max_bad_frames)) {
        return NULL;
    }

    if (!self->channel) {
        PyErr_SetString(PyExc_RuntimeError, "Channel not initialized or has been closed");
        return NULL;
    }

    if (g_context == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "BlackMagic context not initialized");
        return NULL;
    }

    bool success = bm_channel_set_signal_parameters(g_context, self->channel, min_frames, max_bad_frames);
    return PyBool_FromLong(success ? 1 : 0);
}

// Get frame from channel
static PyObject* BMChannel_get_frame(BMChannelObject* self, PyObject* args, PyObject* kwds) {
    static const char* const_kwlist[] = {"format", NULL};
    static char** kwlist = const_cast<char**>(const_kwlist);
    const char* format_str = "rgb";

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &format_str)) {
        return NULL;
    }

    if (!self->channel) {
        PyErr_SetString(PyExc_RuntimeError, "Channel not initialized or has been closed");
        return NULL;
    }

    if (g_context == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "BlackMagic context not initialized");
        return NULL;
    }

    // Determine format
    BMPixelFormat format;
    int channels;

    if (strcmp(format_str, "rgb") == 0) {
        format = BM_FORMAT_RGB;
        channels = 3;
    } else if (strcmp(format_str, "yuv") == 0) {
        format = BM_FORMAT_YUV;
        channels = 2;  // 4:2:2 format, 2 bytes per pixel
    } else if (strcmp(format_str, "gray") == 0 || strcmp(format_str, "grey") == 0) {
        format = BM_FORMAT_GRAY;
        channels = 1;
    } else {
        PyErr_SetString(PyExc_ValueError, "Invalid format. Must be 'rgb', 'yuv', or 'gray'");
        return NULL;
    }

    // Get dimensions
    int width, height;
    size_t buffer_size = bm_get_channel_frame_size(g_context, self->channel, format);

    if (buffer_size == 0) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to determine frame size");
        return NULL;
    }

    // Create NumPy array
    npy_intp dims[3];
    if (format == BM_FORMAT_YUV) {
        // YUV is a special case - it's 4:2:2 format, so width is halved for array shape
        width = self->width / 2;
        height = self->height;
        dims[0] = height;
        dims[1] = width;
        dims[2] = 4;  // 4 bytes per 2 pixels (cb-y0-cr-y1)
    } else {
        width = self->width;
        height = self->height;
        dims[0] = height;
        dims[1] = width;
        if (channels > 1) {
            dims[2] = channels;
        }
    }

    // Create the NumPy array
    PyObject* array;
    if (channels == 1) {
        array = PyArray_SimpleNew(2, dims, NPY_UINT8);
    } else {
        array = PyArray_SimpleNew(3, dims, NPY_UINT8);
    }

    if (!array) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate NumPy array");
        return NULL;
    }

    // Get frame data into the NumPy array
    uint8_t* buffer = (uint8_t*)PyArray_DATA((PyArrayObject*)array);

    if (!bm_get_channel_frame(g_context, self->channel, format, buffer, buffer_size, &width, &height, NULL)) {
        Py_DECREF(array);
        PyErr_SetString(PyExc_RuntimeError, "Failed to get frame data");
        return NULL;
    }

    return array;
}

// Close a channel
static PyObject* BMChannel_close(BMChannelObject* self, PyObject* args) {
    if (self->channel && g_context) {
        bm_stop_channel_capture(g_context, self->channel);
        bm_destroy_channel(g_context, self->channel);
        self->channel = NULL;
    }

    Py_RETURN_NONE;
}


// Module-level methods
static PyMethodDef module_methods[] = {
    {"initialize", (PyCFunction)BMCapture_initialize, METH_NOARGS,
     "Initialize the BlackMagic capture library."},
    {"shutdown", (PyCFunction)BMCapture_shutdown, METH_NOARGS,
     "Shutdown the BlackMagic capture library."},
    {"get_device_count", (PyCFunction)BMCapture_get_device_count, METH_NOARGS,
     "Get number of available BlackMagic devices."},
    {"get_device_name", (PyCFunction)BMCapture_get_device_name, METH_VARARGS,
     "Get name of a BlackMagic device by index."},
    {"get_devices", (PyCFunction)BMCapture_get_devices, METH_NOARGS,
     "Get a list of available BlackMagic devices."},
    {"get_input_ports", (PyCFunction)BMCapture_get_input_ports, METH_VARARGS,
     "Get a list of input ports for a BlackMagic device."},
    {"create_device", (PyCFunction)BMCapture_create_device, METH_VARARGS,
     "Create a BlackMagic device instance."},
    {"select_input_port", (PyCFunction)BMCapture_select_input_port, METH_VARARGS,
     "Select an input port for a BlackMagic device."},
    {"destroy_device", (PyCFunction)BMCapture_destroy_device, METH_VARARGS,
     "Destroy a BlackMagic device instance."},
    {NULL}  /* Sentinel */
};


// Module definition
static struct PyModuleDef bmcapture_module = {
    PyModuleDef_HEAD_INIT,
    "bmcapture",
    "Python interface for BlackMagic capture devices",
    -1,
    module_methods
};

// Module cleanup function
static void bmcapture_module_free(void) {
    // Clean up the global context when the module is unloaded
    if (g_context) {
        bm_free_context(g_context);
        g_context = NULL;
    }
}

// Module initialization
PyMODINIT_FUNC PyInit_bmcapture(void) {
    PyObject* m;

    // Initialize NumPy array API
    import_array();

    // Finalize the type objects
    if (PyType_Ready(&BMCaptureType) < 0)
        return NULL;
    if (PyType_Ready(&BMChannelType) < 0)
        return NULL;

    // Create the module
    m = PyModule_Create(&bmcapture_module);
    if (m == NULL)
        return NULL;

    // Add the BMCapture type
    Py_INCREF(&BMCaptureType);
    if (PyModule_AddObject(m, "BMCapture", (PyObject*)&BMCaptureType) < 0) {
        Py_DECREF(&BMCaptureType);
        Py_DECREF(m);
        return NULL;
    }

    // Add the BMChannel type
    Py_INCREF(&BMChannelType);
    if (PyModule_AddObject(m, "BMChannel", (PyObject*)&BMChannelType) < 0) {
        Py_DECREF(&BMChannelType);
        Py_DECREF(&BMCaptureType);
        Py_DECREF(m);
        return NULL;
    }

    // Add module constants
    PyModule_AddIntConstant(m, "LOW_LATENCY", BM_LOW_LATENCY);
    PyModule_AddIntConstant(m, "NO_FRAME_DROPS", BM_NO_FRAME_DROPS);

    // Initialize the global context
    if (g_context == NULL) {
        g_context = bm_create_context();
    }
    
    // Register cleanup handler
    Py_AtExit(bmcapture_module_free);

    return m;
}
