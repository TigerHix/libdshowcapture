#pragma once

extern "C" {
    void *create_capture();
    int get_devices(void *cap);
    int get_device(void *cap, char *name, int len);
    void open_dialog(void *cap);
    int get_width(void *cap);
    int get_height(void *cap);
    int get_fps(void *cap);
    int capture_device(void *cap, int n, int width, int height, int fps);
    int stop_capture(void *cap);
}