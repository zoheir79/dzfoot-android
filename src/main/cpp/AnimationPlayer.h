#pragma once
#include <cstdint>

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

// Animation IDs (sync with GameBridge.h)
#define ANIM_IDLE       0
#define ANIM_WALK       1
#define ANIM_RUN        2
#define ANIM_SPRINT     3
#define ANIM_SHOOT_R    4
#define ANIM_SHOOT_L    5
#define ANIM_PASS_SHORT 6
#define ANIM_PASS_LONG  7
#define ANIM_TACKLE     8
#define ANIM_DRIBBLE    9
#define ANIM_CELEBRATE  10
#define ANIM_GK_DIVE_L  11
#define ANIM_GK_DIVE_R  12
#define ANIM_GK_CATCH   13
#define ANIM_FALL       14
#define ANIM_HEADER     15
