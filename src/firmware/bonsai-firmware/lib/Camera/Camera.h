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

    // 0 until begin() succeeds. OV3660_PID (0x3660) on the module in the BOM,
    // OV2640_PID (0x26) on a stock XIAO Sense — which is worth being able to
    // tell apart, because the two need different orientation and exposure.
    uint16_t sensorPid() const { return _sensorPid; }

private:
    bool _init(int xclkHz);
    void _applySensorTuning();

    bool     _ready     = false;
    uint16_t _sensorPid = 0;
};
