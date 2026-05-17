#include "AnimationPlayer.h"

static struct { const char* name; float duration; bool loop; } sAnimTable[] = {
    {"idle",       2.0f, true},
    {"walk",       1.2f, true},
    {"run",        0.8f, true},
    {"sprint",     0.6f, true},
    {"shoot_r",    0.9f, false},
    {"shoot_l",    0.9f, false},
    {"pass_short", 0.7f, false},
    {"pass_long",  1.0f, false},
    {"tackle",     1.1f, false},
    {"dribble",    0.5f, true},
    {"celebrate",  2.5f, false},
    {"gk_dive_l",  1.2f, false},
    {"gk_dive_r",  1.2f, false},
    {"gk_catch",   0.8f, false},
    {"fall",       1.0f, false},
    {"header",     0.8f, false},
};

const char* AnimationPlayer::getClipName(uint8_t animId) {
    if (animId < sizeof(sAnimTable)/sizeof(sAnimTable[0]))
        return sAnimTable[animId].name;
    return "idle";
}

float AnimationPlayer::getDuration(uint8_t animId) {
    if (animId < sizeof(sAnimTable)/sizeof(sAnimTable[0]))
        return sAnimTable[animId].duration;
    return 2.0f;
}

bool AnimationPlayer::isLooping(uint8_t animId) {
    if (animId < sizeof(sAnimTable)/sizeof(sAnimTable[0]))
        return sAnimTable[animId].loop;
    return true;
}

void AnimationPlayer::update(float dt) {
    time_ += dt;
    if (blend_ < 1.0f) {
        blend_ += dt * 5.0f;
        if (blend_ > 1.0f) blend_ = 1.0f;
    }
}

void AnimationPlayer::play(uint8_t animId) {
    if (animId == current_) return;
    previous_ = current_;
    current_ = animId;
    blend_ = 0.0f;
    time_ = 0.0f;
}
 
