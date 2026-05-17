#include "AudioManager.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AudioManager", __VA_ARGS__)

bool AudioManager::init() {
    // TODO: alcOpenDevice, alcCreateContext, alcMakeContextCurrent
    LOGI("Audio init (stub)");
    inited_ = true;
    return true;
}

void AudioManager::destroy() {
    // TODO: alcDestroyContext, alcCloseDevice
    inited_ = false;
}

bool AudioManager::loadSound(const char* name, const uint8_t* data, size_t len) {
    // TODO: alGenBuffers, alBufferData with OGG decoder
    LOGI("Load sound %s (%zu bytes)", name, len);
    return true;
}

void AudioManager::playSound(const char* name) {
    if (!inited_) return;
    LOGI("Play %s", name);
    // TODO: alSourcePlay
}

void AudioManager::stopAll() {
    // TODO: alSourceStop for all sources
}

void AudioManager::setVolume(float vol) {
    volume_ = vol;
    // TODO: alListenerf(AL_GAIN, vol)
}

void AudioManager::playLoop(const char* name) {
    currentLoop_ = name;
    playSound(name);
    // TODO: alSourcei(AL_LOOPING, AL_TRUE)
}

void AudioManager::stopLoop() {
    if (!currentLoop_.empty()) {
        // TODO: alSourceStop(loopSource)
        currentLoop_.clear();
    }
}
 
