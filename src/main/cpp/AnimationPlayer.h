#pragma once
#include <cstdint>
#include "protocol/DZFootProtocol.h"

// Maps anim_id to glTF animation clip names
class AnimationPlayer {
public:
    static const char* getClipName(uint8_t animId);
    static float getDuration(uint8_t animId);
    static bool isLooping(uint8_t animId);

    // Blend between two animations
    void update(float dt);
    void play(uint8_t animId);
    uint8_t currentAnim() const { return current_; }

private:
    uint8_t current_ = 0;
    uint8_t previous_ = 0;
    float blend_ = 0.0f;
    float time_ = 0.0f;
};

// Animation IDs now come from dzfoot::AnimId in DZFootProtocol.h
