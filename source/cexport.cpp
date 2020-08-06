#include "../dshowcapture.hpp"
#include "cexport.hpp"

void *create_capture() {
    return 0;
}
int get_devices(void *cap) {
    return 0;
}
int get_device(void *cap, char *name, int len) {
    return 0;
}
void open_dialog(void *cap) {
}
int capture_device(void *cap, int n, int width, int height, int fps) {
    return 0;
}
int get_height(void *cap) {
    return 0;
}
int get_width(void *cap) {
    return 0;
}
int get_fps(void *cap) {
    return 0;
}
int stop_capture(void *cap) {
    return 0;
}