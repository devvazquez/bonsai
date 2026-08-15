#include "Camera.h"
#include "camera_pins.h"
#include <Arduino.h>

namespace {

// Applied only when the sensor really is an OV3660. A stock XIAO Sense ships
// an OV2640, which needs none of this and would be made worse by it.
//
// The vflip is the one that matters: the OV3660 comes out of reset upside
// down. On a webcam that is a nuisance; here the photo goes to a vision model,
// which will describe an inverted scene fluently and confidently, and nothing
// in the answer will look wrong. Espressif's own CameraWebServer example
// carries the same block behind the same PID check.
constexpr int kOv3660Vflip      = 1;
constexpr int kOv3660Hmirror    = 0;
constexpr int kOv3660Saturation = -2;   // stock colours are overcooked

// The example pairs the above with brightness +1, which is a recipe for dark
// rooms. Measured on this hardware it is the wrong default: indoors under a
// ceiling light it clips walls and desks to pure 255, and a clipped region has
// no detail left for the model to read.
constexpr int kOv3660Brightness = 0;

// Which is also why the exposure bias is negative rather than the brightness.
// Brightness is an offset applied after the exposure has been chosen, so it
// cannot recover a blown highlight — it only greys down the whole frame. AE
// level moves the target the auto-exposure aims at, so the highlight never
// clips. Biased dark deliberately: detail survives in shadows, not in white.
constexpr int kOv3660AeLevel = -1;

// The OV3660 pushes more pixels than the OV2640 and, with a single buffer, the
// DMA has nowhere to write the next frame while the JPEG encoder is still on
// the last one. That is the "FB-OVF" spam and the esp_camera_fb_get() timeouts
// this sensor is known for.
constexpr int kFrameBuffers = 2;

}  // namespace

bool Camera::_init(int xclkHz) {
    // Zero-initialised: camera_config_t has fields this function never assigns
    // by name (conv_mode, and sccb_i2c_port below), and leaving them as
    // whatever was on the stack is a rare, unreproducible failure waiting to
    // happen.
    camera_config_t config = {};

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn      = PWDN_GPIO_NUM;
    config.pin_reset     = RESET_GPIO_NUM;
    config.sccb_i2c_port = -1;          // -1 = the driver brings up its own bus
    config.xclk_freq_hz  = xclkHz;
    config.pixel_format  = PIXFORMAT_JPEG;
    config.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location   = CAMERA_FB_IN_PSRAM;
    config.frame_size    = FRAMESIZE_UXGA;
    config.jpeg_quality  = 12;
    config.fb_count      = kFrameBuffers;

    const esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init at %d MHz: %s\n", xclkHz / 1000000,
                      esp_err_to_name(err));
        return false;
    }
    return true;
}

bool Camera::begin() {
    if (!_init(20000000)) {
        // Halving the pixel clock is the documented first move when the sensor
        // outruns the JPEG encoder. It costs frame rate, which a device that
        // takes one photo per button press does not spend.
        Serial.println("Retrying the camera at 10 MHz...");
        esp_camera_deinit();
        delay(100);
        if (!_init(10000000)) return false;
    }

    _ready = true;
    _applySensorTuning();
    return true;
}

void Camera::_applySensorTuning() {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;

    _sensorPid = s->id.PID;
    if (_sensorPid != OV3660_PID) {
        // Said out loud because "the descriptions are odd" and "the module
        // fitted is not the one you think" look identical from the outside.
        Serial.printf("Camera sensor PID 0x%04x, not an OV3660: leaving "
                      "orientation and exposure alone.\n", _sensorPid);
        return;
    }

    s->set_vflip(s, kOv3660Vflip);
    s->set_hmirror(s, kOv3660Hmirror);
    s->set_saturation(s, kOv3660Saturation);
    s->set_brightness(s, kOv3660Brightness);
    s->set_ae_level(s, kOv3660AeLevel);
    Serial.println("Camera sensor OV3660: flipped upright, exposure biased down.");
}

void Camera::end() {
    esp_camera_deinit();
    _ready = false;
}

void Camera::setFrameSize(framesize_t size){
    if(!_ready) return;
    sensor_t * s = esp_camera_sensor_get();
    s->set_framesize(s, size);
}

camera_fb_t* Camera::capture() {
    if (!_ready) return nullptr;

    // With CAMERA_GRAB_WHEN_EMPTY the driver may hand back up to fb_count
    // frames captured before this call — the docs say so plainly. On a device
    // that photographs whatever is in front of it the moment a button is
    // pressed, that means describing the previous room, and it is a bug that
    // leaves no trace: the answer reads perfectly sensibly, it is just about
    // somewhere the wearer has already left.
    //
    // So the queue is emptied first and the next frame is the real one. It
    // costs a couple of frame times, which is the correct price for the photo
    // being of now.
    for (int i = 0; i < kFrameBuffers; ++i) {
        camera_fb_t* stale = esp_camera_fb_get();
        if (!stale) return nullptr;
        esp_camera_fb_return(stale);
    }
    return esp_camera_fb_get();
}

void Camera::release(camera_fb_t* fb) {
    if (fb) esp_camera_fb_return(fb);
}

bool Camera::save(fs::FS& fs, const char* path) {
    camera_fb_t* fb = capture();
    if (!fb) return false;

    File file = fs.open(path, FILE_WRITE);
    if (!file) {
        release(fb);
        return false;
    }

    bool ok = file.write(fb->buf, fb->len) == fb->len;
    file.close();
    release(fb);
    return ok;
}