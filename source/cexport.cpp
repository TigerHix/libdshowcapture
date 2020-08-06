#include <windows.h>
#include "../dshowcapture.hpp"
#include "cexport.hpp"
#include <iostream>

using namespace std;
using namespace DShow;

struct Context {
    Device device;
    std::vector<VideoDevice> devices;
    VideoConfig config;
    HANDLE writeReady;
    HANDLE readReady;
    CRITICAL_SECTION busy;
    int capturing;
    int debug;
    unsigned char *buffer;
    int size;
};

static int initialized = 0;

void DSHOWCAPTURE_EXPORT *create_capture() {
    if (initialized == 0) {
        CoInitialize(0);
        initialized = 1;
    }
    Context *context = new Context();
    context->writeReady = CreateEventA(0, FALSE, FALSE, NULL);
    context->readReady = CreateEventA(0, FALSE, FALSE, NULL);
    InitializeCriticalSection(&context->busy);
    SetEvent(context->writeReady);
    context->capturing = 0;
    context->debug = 0;
    context->buffer = 0;
    context->size = 0;
    return context;
}
int DSHOWCAPTURE_EXPORT get_devices(void *cap) {
    Context *context = (Context*)cap;
    Device::EnumVideoDevices(context->devices);
    return context->devices.size();
}

// https://stackoverflow.com/a/52488521
std::string WidestringToString(std::wstring wstr)
{
    if (wstr.empty())
    {
        return std::string();
    }
#if defined WIN32
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &wstr[0], wstr.size(), NULL, 0, NULL, NULL);
    std::string ret = std::string(size, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &wstr[0], wstr.size(), &ret[0], size, NULL, NULL);
#else
    size_t size = 0;
    _locale_t lc = _create_locale(LC_ALL, "en_US.UTF-8");
    errno_t err = _wcstombs_s_l(&size, NULL, 0, &wstr[0], _TRUNCATE, lc);
    std::string ret = std::string(size, 0);
    err = _wcstombs_s_l(&size, &ret[0], size, &wstr[0], _TRUNCATE, lc);
    _free_locale(lc);
    ret.resize(size - 1);
#endif
    return ret;
}

void DSHOWCAPTURE_EXPORT get_device(void *cap, int n, char *name, int len) {
    Context *context = (Context*)cap;
    strncpy(name, WidestringToString(context->devices[n].name).c_str(), len);
}
void DSHOWCAPTURE_EXPORT open_dialog(void *cap) {
}

void capture_callback(const VideoConfig &config, unsigned char *data,
    size_t size, long long startTime, long long stopTime,
    long rotation) {
    Context *context = (Context*)config.context;
    float start = (float)startTime / 10000000.;
    float stop = (float)stopTime / 10000000.;
    if (context->debug == 2)
        cerr << "[Size: " << size << " Start: " << start << " End: " << stop << " Rotation: " << rotation << "]\n";
    if (size != config.cx * abs(config.cy_abs) * 4)
        return;
    if (context->debug == 1)
        cerr << "[Size: " << size << " Start: " << start << " End: " << stop << " Rotation: " << rotation << "]\n";
    if (WaitForSingleObject(context->writeReady, 20) != WAIT_OBJECT_0)
        return;
    EnterCriticalSection(&context->busy);
    if (size != context->size) {
        if (context->buffer != 0)
            delete context->buffer;
        context->buffer = new unsigned char[size];
        context->size = size;
    }
    memcpy(context->buffer, data, size);
    LeaveCriticalSection(&context->busy);
    SetEvent(context->readReady);
}

int DSHOWCAPTURE_EXPORT capture_device(void *cap, int n, int width, int height, int fps) {
    Context *context = (Context*)cap;
    if (context->devices.size() < 1)
        get_devices(cap);

    context->config.name = context->devices[n].name.c_str();
    context->config.path = context->devices[n].path.c_str();

    VideoDevice dev = context->devices[n];
    int best_match = -1;
    int best_dx = 0;
    int best_dy = 0;
    unsigned int score = 0xffffffff;
    for (int i = 0; i < dev.caps.size(); i++) {
        if (dev.caps[i].minCX <= width && dev.caps[i].maxCX >= width && dev.caps[i].minCY <= height && dev.caps[i].maxCY >= height) {
            best_match = i;
            break;
        }
        int dx = 0;
        if (dev.caps[i].minCX > width)
            dx = dev.caps[i].minCX - width;
        if (dev.caps[i].maxCX < width)
            dx = width - dev.caps[i].maxCX;
        dx = dx * dx;
        int dy = 0;
        if (dev.caps[i].minCY > height)
            dy = dev.caps[i].minCY - height;
        if (dev.caps[i].maxCY < height)
            dy = height - dev.caps[i].maxCY;
        dy = dy * dy;
        int mismatch = dx + dy;
        if (mismatch < score) {
            score = mismatch;
            best_match = i;
            best_dx = dx;
            best_dy = dy;
        }
    }

    int unit = 10000000;
    int interval = unit / fps;
    if (best_match >= -1) {
        if (interval < dev.caps[best_match].minInterval)
            interval = dev.caps[best_match].minInterval;
        if (interval > dev.caps[best_match].maxInterval)
            interval = dev.caps[best_match].maxInterval;
    }

    context->config.context = cap;
    context->config.callback = capture_callback;
    context->config.cx = width;
    context->config.cy_abs = height;
    context->config.cy_flip = false;
    context->config.frameInterval = interval;
    context->config.format = VideoFormat::XRGB;
    context->config.internalFormat = VideoFormat::XRGB;
    context->config.useDefaultConfig = false;
    context->capturing = 0;
    if (!context->device.ResetGraph())
        return 0;
    if (!context->device.Valid())
        return 0;
    if (!context->device.SetVideoConfig(&context->config)) {
        context->config.internalFormat = VideoFormat::Any;
        if (!context->device.SetVideoConfig(&context->config))
            return 0;
    }
    if (!context->device.Valid())
        return 0;
    if (!context->device.ConnectFilters())
        return 0;
    if (!context->device.Valid())
        return 0;
    context->capturing = context->device.Start() == Result::Success;
    if (context->capturing && context->config.format != VideoFormat::XRGB) {
        context->device.Stop();
        context->capturing = 0;
        return 0;
    }
    SetEvent(context->writeReady);
    return context->capturing;
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
    return 10000000 / context->config.frameInterval;
}
int DSHOWCAPTURE_EXPORT get_flipped(void *cap) {
    Context *context = (Context*)cap;
    return context->config.cy_flip;
}
int DSHOWCAPTURE_EXPORT get_colorspace(void *cap) {
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
    if (context->size != size) {
        return 0;
        LeaveCriticalSection(&context->busy);
    }
    memcpy(buffer, context->buffer, size);
    LeaveCriticalSection(&context->busy);
    SetEvent(context->writeReady);
    return 1;
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
    CloseHandle(context->writeReady);
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
    for (int i = 0; i < context->devices[n].caps.size(); i++) {
        VideoInfo cap = context->devices[n].caps[i];
        cout << "Caps " << i << ": XRange: " << cap.minCX << "-" << cap.maxCX << " YRange: " << cap.minCY << "-" << cap.maxCY << " IRange: " << cap.minInterval << "-" << cap.maxInterval << " Format: " << (int)cap.format << "\n";
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