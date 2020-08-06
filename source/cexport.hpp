#pragma once

extern "C" {
    void DSHOWCAPTURE_EXPORT *create_capture();
    int DSHOWCAPTURE_EXPORT get_devices(void *cap);
    void DSHOWCAPTURE_EXPORT get_device(void *cap, int n, char *name, int len);
    //void DSHOWCAPTURE_EXPORT open_dialog(void *cap);
    int DSHOWCAPTURE_EXPORT get_width(void *cap);
    int DSHOWCAPTURE_EXPORT get_height(void *cap);
    int DSHOWCAPTURE_EXPORT get_fps(void *cap);
    int DSHOWCAPTURE_EXPORT get_flipped(void *cap);
    int DSHOWCAPTURE_EXPORT get_colorspace(void *cap);
    int DSHOWCAPTURE_EXPORT capture_device(void *cap, int n, int width, int height, int fps);
    int DSHOWCAPTURE_EXPORT get_frame(void *cap, int timeout, unsigned char *buffer, int size);
    void DSHOWCAPTURE_EXPORT stop_capture(void *cap);
    void DSHOWCAPTURE_EXPORT destroy_capture(void *cap);
    int DSHOWCAPTURE_EXPORT capturing(void *cap);
    void DSHOWCAPTURE_EXPORT lib_test(int n, int width, int height, int fps);
}