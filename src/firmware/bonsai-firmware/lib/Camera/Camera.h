#pragma once

#include <esp_camera.h>
#include <FS.h>

class Camera {
public:
    bool begin();
    void end();
    void setFrameSize(framesize_t size);

    camera_fb_t* capture();
    void release(camera_fb_t* fb);
    bool save(fs::FS& fs, const char* path);

    bool isReady() const { return _ready; }

private:
    bool _ready = false;
};
