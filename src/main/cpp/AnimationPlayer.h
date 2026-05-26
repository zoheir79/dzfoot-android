#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "protocol/DZFootProtocol.h"

// ─── Runtime animation structures ────────────────────────────────

struct AnimKeyframe {
    float time;
    float pos[3];
    float rot[4]; // quaternion x,y,z,w
};

struct AnimTrack {
    std::string boneName;
    std::vector<AnimKeyframe> keyframes;
};

struct AnimClip {
    std::string name;
    float duration;
    std::vector<AnimTrack> tracks; // one per bone
};

// ─── AnimationPlayer ───────────────────────────────────────────
// Loads binary animation clips (anim_templates.bin) and evaluates
// blended bone matrices for GPU skinning each frame.

class AnimationPlayer {
public:
    bool loadFromBinary(const uint8_t* data, size_t len);

    // Call each frame with delta-time. Updates internal crossfade state.
    void update(float dt);

    // Switch to a new animation (crossfade over 0.2s)
    void play(uint8_t animId);

    // Evaluate current blended pose into boneMatrices[16*numBones]
    // Returns number of bones written.
    int evaluate(float* boneMatrices, int maxBones) const;

    uint8_t currentAnim() const { return current_; }
    float currentTime() const { return time_; }

    static const char* getClipName(uint8_t animId);
    static float getDuration(uint8_t animId);
    static bool isLooping(uint8_t animId);

private:
    std::vector<AnimClip> clips_; // index = anim_id
    uint8_t current_ = 0;
    uint8_t previous_ = 0;
    float blend_ = 1.0f;   // 0 = all previous, 1 = all current
    float time_ = 0.0f;
    float prevTime_ = 0.0f;
    static constexpr float CROSSFADE_DURATION = 0.2f;

    static void slerp(const float* a, const float* b, float t, float* out);
    static void lerpVec3(const float* a, const float* b, float t, float* out);
    static void quatToMat4(const float* q, float* m);
    static void mat4Mul(const float* a, const float* b, float* out);
    static void mat4Identity(float* m);

    void evaluateClip(const AnimClip& clip, float t, float* outPos, float* outRot,
                      const std::string& boneName) const;
    int findBoneIndex(const std::string& name) const;
};
