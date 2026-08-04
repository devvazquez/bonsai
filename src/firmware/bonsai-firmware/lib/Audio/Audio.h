#pragma once

enum class DefaultAudios {
    NO_WIFI,
    START_TALKING,
};
    
class Audio {
public:
    void playDefault(DefaultAudios audio);


private:
    bool _playingAudio = false;
};
