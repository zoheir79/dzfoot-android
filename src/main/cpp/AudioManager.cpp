#include "AudioManager.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AudioManager", __VA_ARGS__)

// Forward declarations for JNI audio bridge (defined in jni_main.cpp)
extern void androidPlaySound(const char* name);
extern void androidStopAllSounds();
extern void androidSetVolume(float vol);
extern void androidPlayLoop(const char* name);
extern void androidStopLoop();

bool AudioManager::init() {
    LOGI("Audio init via JNI bridge");
    inited_ = true;
    return true;
}

void AudioManager::destroy() {
    inited_ = false;
}

bool AudioManager::loadSound(const char* name, const uint8_t* data, size_t len) {
    LOGI("Load sound %s (%zu bytes)", name, len);
    return true;
}

void AudioManager::playSound(const char* name) {
    if (!inited_) return;
    androidPlaySound(name);
}

void AudioManager::stopAll() {
    androidStopAllSounds();
}

void AudioManager::setVolume(float vol) {
    volume_ = vol;
    androidSetVolume(vol);
}

void AudioManager::playLoop(const char* name) {
    currentLoop_ = name;
    androidPlayLoop(name);
}

void AudioManager::stopLoop() {
    if (!currentLoop_.empty()) {
        androidStopLoop();
        currentLoop_.clear();
    }
}
 
