#pragma once
#include <string>
#include <map>

// OpenAL-soft stub wrapper for Android
class AudioManager {
public:
    bool init();
    void destroy();

    bool loadSound(const char* name, const uint8_t* data, size_t len);
    void playSound(const char* name);
    void stopAll();
    void setVolume(float vol);

    // Background loop
    void playLoop(const char* name);
    void stopLoop();

private:
    bool inited_ = false;
    float volume_ = 1.0f;
    std::string currentLoop_;
    // TODO: OpenAL buffer/source handles per sound
};
