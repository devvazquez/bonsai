#include "Audio.h"
#include <Arduino.h>

void Audio::playDefault(DefaultAudios audio) {
    Serial.print("Playing default audio: ");
    Serial.println(static_cast<int>(audio));
}