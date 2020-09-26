#include <windows.h>
#include "../dshowcapture.hpp"
#include "cexport.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace std;
using namespace DShow;

struct Context {
    Device device;
    vector<VideoDevice> devices;
    VideoConfig config;
    HANDLE readReady;
    CRITICAL_SECTION busy;
    int capturing;
    int debug;
    unsigned char *buffer;
    size_t size;
    string json;
};

static int initialized = 0;

void DSHOWCAPTURE_EXPORT *create_capture() {
    if (initialized == 0) {
        CoInitialize(0);
        initialized = 1;
    }
    Context *context = new Context();
    context->readReady = CreateEventA(0, FALSE, FALSE, NULL);
    InitializeCriticalSection(&context->busy);
    context->capturing = 0;
    context->debug = 0;
    context->buffer = 0;
    context->size = 0;
    return context;
}
int DSHOWCAPTURE_EXPORT get_devices(void *cap) {
    Context *context = (Context*)cap;
    Device::EnumVideoDevices(context->devices);
    return (int)context->devices.size();
}

string escape(string str) {
    std::ostringstream o;
    for (auto c = str.cbegin(); c != str.cend(); c++) {
        if (*c == '"' || *c == '\\' || ('\x00' <= *c && *c <= '\x1f')) {
            o << "\\u"
                << std::hex << std::setw(4) << std::setfill('0') << (int)*c;
        }
        else {
            o << *c;
        }
    }
    return o.str();
}

// https://stackoverflow.com/a/52488521
string WidestringToString(wstring wstr)
{
    if (wstr.empty())
    {
        return string();
    }
#if defined WIN32
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    string ret = string(size, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &wstr[0], (int)wstr.size(), &ret[0], size, NULL, NULL);
#else
    size_t size = 0;
    _locale_t lc = _create_locale(LC_ALL, "en_US.UTF-8");
    errno_t err = _wcstombs_s_l(&size, NULL, 0, &wstr[0], _TRUNCATE, lc);
    string ret = string(size, 0);
    err = _wcstombs_s_l(&size, &ret[0], size, &wstr[0], _TRUNCATE, lc);
    _free_locale(lc);
    ret.resize(size - 1);
#endif
    return ret;
}

static inline int GetFormatRating(VideoFormat format)
{
    if (format == VideoFormat::XRGB)
        return 0;
    else if (format == VideoFormat::ARGB)
        return 1;
    else if (format == VideoFormat::Y800)
        return 12;
    else if (format == VideoFormat::HDYC)
        return 15;
    else if (format >= VideoFormat::I420 && format <= VideoFormat::YV12)
        return 5;
    else if (format >= VideoFormat::YVYU && format <= VideoFormat::UYVY)
        return 2;
    else if (format == VideoFormat::MJPEG)
        return 10;
    else if (format == VideoFormat::H264)
        return 15;

    return 15;
}

int DSHOWCAPTURE_EXPORT get_json_length(void *cap) {
    Context *context = (Context*)cap;
    Device::EnumVideoDevices(context->devices);

    ostringstream ss;
    ss << "[";
    for (size_t dev = 0; dev < context->devices.size(); dev++) {
        if (dev > 0)
            ss << ",";
        ss << "{\"id\": " << dev << ",";
        ss << "\"name\": \"" << escape(WidestringToString(context->devices[dev].name)) << "\",";
        ss << "\"path\": \"" << escape(WidestringToString(context->devices[dev].path)) << "\",";
        ss << "\"caps\": [";
        for (size_t dcap = 0; dcap < context->devices[dev].caps.size(); dcap++) {
            if (GetFormatRating(context->devices[dev].caps[dcap].format) >= 15)
                continue;
            if (dcap > 0)
                ss << ",";
            ss << "{";
            ss << "\"id\": " << dcap << ",";
            ss << "\"minCX\": " << context->devices[dev].caps[dcap].minCX << ",";
            ss << "\"minCY\": " << abs(context->devices[dev].caps[dcap].minCY) << ",";
            ss << "\"maxCX\": " << context->devices[dev].caps[dcap].maxCX << ",";
            ss << "\"maxCY\": " << abs(context->devices[dev].caps[dcap].maxCY) << ",";
            ss << "\"granularityCX\": " << context->devices[dev].caps[dcap].granularityCX << ",";
            ss << "\"granularityCY\": " << abs(context->devices[dev].caps[dcap].granularityCY) << ",";
            ss << "\"minInterval\": " << context->devices[dev].caps[dcap].minInterval << ",";
            ss << "\"maxInterval\": " << context->devices[dev].caps[dcap].maxInterval << ",";
            ss << "\"format\": " << (int)(context->devices[dev].caps[dcap].format);
            ss << "}";
        }
        ss << "]}";
    }
    ss << "]";
    context->json = ss.str();
    return (int)context->json.length();
}

void DSHOWCAPTURE_EXPORT get_json(void *cap, char *buffer, int len) {
    Context *context = (Context*)cap;
    strncpy(buffer, context->json.c_str(), len);
}

void DSHOWCAPTURE_EXPORT get_device(void *cap, int n, char *name, int len) {
    Context *context = (Context*)cap;
    strncpy(name, WidestringToString(context->devices[n].name).c_str(), len);
}
/*void DSHOWCAPTURE_EXPORT open_dialog(void *cap) {
    Context *context = (Context*)cap;
}*/

void capture_callback(const VideoConfig &config, unsigned char *data,
    size_t size, long long startTime, long long stopTime,
    long rotation) {
    Context *context = (Context*)config.context;
    float start = (float)startTime / 10000000.f;
    float stop = (float)stopTime / 10000000.f;
    if (context->debug == 2)
        cerr << "[Size: " << size << " Start: " << start << " End: " << stop << " Rotation: " << rotation << "]\n";
    if (size > (size_t)config.cx * abs(config.cy_abs) * 4)
        return;
    if (context->debug == 1)
        cerr << "[Size: " << size << " Start: " << start << " End: " << stop << " Rotation: " << rotation << "]\n";
    EnterCriticalSection(&context->busy);
    if (size > context->size) {
        if (context->buffer != 0)
            delete context->buffer;
        context->buffer = new unsigned char[size];
        context->size = size;
    }
    memcpy(context->buffer, data, size);
    LeaveCriticalSection(&context->busy);
    SetEvent(context->readReady);
}

int DSHOWCAPTURE_EXPORT capture_device_default(void *cap, int n) {
    Context *context = (Context*)cap;
    if (context->devices.size() < 1)
        get_devices(cap);

    context->config.name = context->devices[n].name.c_str();
    context->config.path = context->devices[n].path.c_str();
    context->config.context = cap;
    context->config.callback = capture_callback;
    context->config.format = VideoFormat::Any;
    context->config.internalFormat = VideoFormat::Any;
    context->config.cx = 0;
    context->config.cy_abs = 0;
    context->config.cy_flip = false;
    context->config.frameInterval = 0;
    context->config.useDefaultConfig = true;
    context->capturing = 0;

    VideoConfig config = context->config;

    if (!context->device.ResetGraph())
        return 0;
    if (!context->device.Valid())
        return 0;
    if (!context->device.SetVideoConfig(&context->config)) {
        return 0;
    }
    if (context->config.internalFormat > VideoFormat::MJPEG) {
        cout << "Detected unsupported encoded format " << (int)context->config.internalFormat << ", trying to downgrade to MJPEG\n\n";
        context->config = config;
        context->config.internalFormat = VideoFormat::MJPEG;
        if (!context->device.SetVideoConfig(&context->config)) {
            return 0;
        }
    }
    if (!context->device.Valid())
        return 0;
    if (!context->device.ConnectFilters())
        return 0;
    if (!context->device.Valid())
        return 0;
    context->capturing = context->device.Start() == Result::Success;
    long long unit = 10000000;
    cout << "Final camera configuration: " << context->config.cx << "x" << context->config.cy_abs << " " << unit / context->config.frameInterval << "\n";
    cout << "Format: " << (int)context->config.format << " Internal format: " << (int)context->config.internalFormat << "\n";
    return context->capturing;
}

static inline void ClampToGranularity(int &val, int minVal, int granularity)
{
    val -= ((val - minVal) % granularity);
}

int DSHOWCAPTURE_EXPORT capture_device_by_dcap(void *cap, int n, int dcap, int cx, int cy, long long interval) {
    Context *context = (Context*)cap;
    if (context->devices.size() < 1)
        get_devices(cap);

    if (context->devices.size() <= n) {
        cout << "Invalid device number " << n << "\n";
        return 0;
    }
    if (context->devices[n].caps.size() <= dcap) {
        cout << "Invalid device capability number " << dcap << "\n";
        return 0;
    }

    ClampToGranularity(cx, context->devices[n].caps[dcap].minCX, context->devices[n].caps[dcap].granularityCX);
    int cy_abs = abs(cy);
    ClampToGranularity(cy_abs, abs(context->devices[n].caps[dcap].minCY), abs(context->devices[n].caps[dcap].granularityCY));

    if (cx < context->devices[n].caps[dcap].minCX)
        cx = context->devices[n].caps[dcap].minCX;
    if (cx > context->devices[n].caps[dcap].maxCX)
        cx = context->devices[n].caps[dcap].maxCX;

    if (cy_abs < abs(context->devices[n].caps[dcap].minCY))
        cy_abs = abs(context->devices[n].caps[dcap].minCY);
    if (cy_abs > abs(context->devices[n].caps[dcap].maxCY))
        cy_abs = abs(context->devices[n].caps[dcap].maxCY);

    if (interval < context->devices[n].caps[dcap].minInterval)
        interval = context->devices[n].caps[dcap].minInterval;
    if (interval > context->devices[n].caps[dcap].maxInterval)
        interval = context->devices[n].caps[dcap].maxInterval;

    int flip = 0;
    if (context->devices[n].caps[dcap].minCY < 0 || context->devices[n].caps[dcap].maxCY < 0)
        flip = 1;

    context->config.name = context->devices[n].name.c_str();
    context->config.path = context->devices[n].path.c_str();
    context->config.context = cap;
    context->config.callback = capture_callback;
    context->config.format = VideoFormat::Any;
    context->config.internalFormat = context->devices[n].caps[dcap].format;
    context->config.cx = cx;
    context->config.cy_abs = cy_abs;
    context->config.cy_flip = flip;
    context->config.frameInterval = interval;
    context->config.useDefaultConfig = false;
    context->capturing = 0;

    VideoConfig config = context->config;

    if (!context->device.ResetGraph())
        return 0;
    if (!context->device.Valid())
        return 0;
    if (!context->device.SetVideoConfig(&context->config)) {
        return 0;
    }
    if (context->config.internalFormat > VideoFormat::MJPEG) {
        cout << "Detected unsupported encoded format " << (int)context->config.internalFormat << ", trying to downgrade to MJPEG\n\n";
        context->config = config;
        context->config.internalFormat = VideoFormat::MJPEG;
        if (!context->device.SetVideoConfig(&context->config)) {
            return 0;
        }
    }
    if (!context->device.Valid())
        return 0;
    if (!context->device.ConnectFilters())
        return 0;
    if (!context->device.Valid())
        return 0;
    context->capturing = context->device.Start() == Result::Success;
    long long unit = 10000000;
    cout << "Final camera configuration: " << context->config.cx << "x" << context->config.cy_abs << " " << unit / context->config.frameInterval << "\n";
    cout << "Format: " << (int)context->config.format << " Internal format: " << (int)context->config.internalFormat << "\n";
    return context->capturing;
}

int DSHOWCAPTURE_EXPORT capture_device(void *cap, int n, int width, int height, int fps) {
    Context *context = (Context*)cap;
    int ret = 1;
    if (context->devices.size() < 1)
        get_devices(cap);

    context->config.name = context->devices[n].name.c_str();
    context->config.path = context->devices[n].path.c_str();

    VideoDevice dev = context->devices[n];
    int best_match = -1;
    int best_width = width;
    int best_height = height;
    VideoFormat best_format = VideoFormat::XRGB;
    float aspect = (float)width / (float)height;
    long long unit = 10000000;
    long long interval = unit / fps;
    long long best_interval = interval;
    unsigned int score = 0xffffffff;
    for (size_t i = 0; i < dev.caps.size(); i++) {
        int dx = 0;
        int dy = 0;
        int this_width = width;
        int this_height = width;

        if (dev.caps[i].minCX > width)
            this_width = dev.caps[i].minCX;
        if (dev.caps[i].maxCX < width)
            this_width = dev.caps[i].maxCX;
        if (dev.caps[i].minCY > height)
            this_height = dev.caps[i].minCY;
        if (dev.caps[i].maxCY < height)
            this_height = dev.caps[i].maxCY;

        dx = width - this_width;
        dx = dx * dx;
        dy = height - this_height;
        dy = dy * dy;

        if (dx < dy) {
            this_height = (int)(this_width / aspect + 0.5);
            if (dev.caps[i].minCY > this_height)
                this_height = dev.caps[i].minCY;
            if (dev.caps[i].maxCY < this_height)
                this_height = dev.caps[i].maxCY;
        } else {
            this_width = (int)(this_height * aspect + 0.5);
            if (dev.caps[i].minCY > this_height)
                this_width = dev.caps[i].minCY;
            if (dev.caps[i].maxCY < this_height)
                this_width = dev.caps[i].maxCY;
        }

        if (dev.caps[i].minCX > width)
            this_width = dev.caps[i].minCX;
        if (dev.caps[i].maxCX < width)
            this_width = dev.caps[i].maxCX;
        if (dev.caps[i].minCY > height)
            this_height = dev.caps[i].minCY;
        if (dev.caps[i].maxCY < height)
            this_height = dev.caps[i].maxCY;

        unsigned int mismatch = (width * height) - (this_width * this_height);
        mismatch = mismatch * mismatch;

        int format_rating = GetFormatRating(dev.caps[i].format);
        if (mismatch < score && format_rating < 15) {
            score = mismatch;
            best_match = (int)i;
            best_width = this_width;
            best_height = this_height;
            best_format = dev.caps[i].format;
            if (best_interval < dev.caps[i].minInterval)
                best_interval = dev.caps[i].minInterval;
            if (best_interval > dev.caps[i].maxInterval)
                best_interval = dev.caps[i].maxInterval;
            int di = (int)abs(interval - best_interval);
            score += di * 10 + format_rating;
        }
    }

    context->config.context = cap;
    context->config.callback = capture_callback;
    context->config.cx = best_width;
    context->config.cy_abs = best_height;
    context->config.cy_flip = false;
    context->config.frameInterval = best_interval;
    cout << "Camera configuration: " << best_width << "x" << best_height << " " << best_interval << " " << (int)best_format << "\n";
    context->config.format = VideoFormat::Any;
    context->config.internalFormat = best_format;
    context->config.useDefaultConfig = false;
    context->capturing = 0;
    if (ret && !context->device.ResetGraph())
        ret = 0;
    if (ret && !context->device.Valid())
        ret = 0;
    if (ret && (!context->device.SetVideoConfig(&context->config) || !context->device.Valid() || !context->device.ConnectFilters())) {
        cout << "Retrying with any format\n";
        context->config.internalFormat = VideoFormat::Any;
        if (!context->device.SetVideoConfig(&context->config) || !context->device.Valid() || !context->device.ConnectFilters())
            ret = 0;
    }
    if (ret && !context->device.Valid())
        ret = 0;
    if (ret) {
        context->capturing = context->device.Start() == Result::Success;
        cout << "Final camera configuration: " << context->config.cx << "x" << context->config.cy_abs << " " << unit / context->config.frameInterval << "\n";
        cout << "Format: " << (int)context->config.format << " Internal format: " << (int)context->config.internalFormat << "\n";
        if (ret)
            return context->capturing;
    }
    cout << "Failed\n";
    context->capturing = 0;
    context->device.Stop();
    return capture_device_default(cap, n);
}
int DSHOWCAPTURE_EXPORT get_width(void *cap) {
    Context *context = (Context*)cap;
    return context->config.cx;
}
int DSHOWCAPTURE_EXPORT get_height(void *cap) {
    Context *context = (Context*)cap;
    return abs(context->config.cy_abs);
}
int DSHOWCAPTURE_EXPORT get_fps(void *cap) {
    Context *context = (Context*)cap;
    if (context->config.frameInterval == 0)
        return 1;
    return (int)(10000000 / context->config.frameInterval);
}
int DSHOWCAPTURE_EXPORT get_flipped(void *cap) {
    Context *context = (Context*)cap;
    return context->config.cy_flip;
}
int DSHOWCAPTURE_EXPORT get_colorspace(void *cap) {
    Context *context = (Context*)cap;
    return (int)context->config.format;
}
int DSHOWCAPTURE_EXPORT get_colorspace_internal(void *cap) {
    Context *context = (Context*)cap;
    return (int)context->config.internalFormat;
}
int DSHOWCAPTURE_EXPORT get_frame(void *cap, int timeout, unsigned char *buffer, int size) {
    Context *context = (Context*)cap;
    if (!context->capturing)
        return 0;
    if (WaitForSingleObject(context->readReady, timeout) != WAIT_OBJECT_0)
        return 0;
    EnterCriticalSection(&context->busy);
    if (context->size > (size_t)size) {
        LeaveCriticalSection(&context->busy);
        return 0;
    }
    memcpy(buffer, context->buffer, context->size);
    LeaveCriticalSection(&context->busy);
    return context->size;
}
int DSHOWCAPTURE_EXPORT get_size(void *cap) {
    Context *context = (Context*)cap;
    return (int)context->size;
}
void DSHOWCAPTURE_EXPORT stop_capture(void *cap) {
    Context *context = (Context*)cap;
    context->device.Stop();
    context->capturing = 0;
}
void DSHOWCAPTURE_EXPORT destroy_capture(void *cap) {
    Context *context = (Context*)cap;
    if (context->capturing)
        stop_capture(cap);
    CloseHandle(context->readReady);
    DeleteCriticalSection(&context->busy);
    if (context->buffer)
        delete context->buffer;
    context->devices.clear();
    delete context;
}
int DSHOWCAPTURE_EXPORT capturing(void *cap) {
    Context *context = (Context*)cap;
    return context->capturing;
}
void DSHOWCAPTURE_EXPORT lib_test(int n, int width, int height, int fps) {
    void *cap = create_capture();
    int num = get_devices(cap);
    cout << "Number: " << num << "\n";
    for (int i = 0; i < num; i++) {
        char cam_name[255];
        get_device(cap, i, cam_name, 255);
        cout << "Cam " << i << ": " << cam_name << "\n";
    }

    Context *context = (Context*)cap;
    context->debug = 1;
    for (size_t i = 0; i < context->devices[n].caps.size(); i++) {
        VideoInfo caps = context->devices[n].caps[i];
        cout << "Caps " << i << ": XRange: " << caps.minCX << "-" << caps.maxCX << " YRange: " << caps.minCY << "-" << caps.maxCY << " IRange: " << caps.minInterval << "-" << caps.maxInterval << " Format: " << (int)caps.format << "\n";
    }

    cout << "Start: " << capture_device(cap, n, width, height, fps) << "\n";
    width = get_width(cap);
    height = get_height(cap);
    unsigned char *buffer = new unsigned char[width * height * 4];
    cout << "Width: " << width << "\n";
    cout << "Height: " << height << "\n";
    cout << "Flipped: " << get_flipped(cap) << "\n";
    cout << "Fps: " << get_fps(cap) << "\n";
    cout << "Internal colorspace: " << get_colorspace(cap) << "\n";
    for (int i = 0; i < 40 && capturing(cap);) {
        int r = get_frame(cap, 1000, buffer, width * height * 4);
        if (r) {
            i++;
            cout << "Got frame\n";
        } else
            cout << "Lost frame\n";
    }
    destroy_capture(cap);
}