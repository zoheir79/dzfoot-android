#include "AnimationPlayer.h"
#include <cmath>
#include <cstring>
#include <android/log.h>

#define LOG_TAG "AnimationPlayer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static struct { const char* name; float duration; bool loop; } sAnimTable[] = {
    {"idle",       2.0f, true},
    {"walk",       1.2f, true},
    {"run",        0.8f, true},
    {"sprint",     0.6f, true},
    {"shoot_r",    0.9f, false},
    {"shoot_l",    0.9f, false},
    {"pass_short", 0.7f, false},
    {"pass_long",  1.0f, false},
    {"header",     0.8f, false},
    {"tackle",     1.1f, false},
    {"dribble",    0.5f, true},
    {"fall",       1.0f, false},
    {"celebrate",  2.5f, false},
    {"gk_idle",    2.0f, true},
    {"gk_dive_l",  1.2f, false},
    {"gk_dive_r",  1.2f, false},
    {"gk_catch",   0.8f, false},
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

// ─── Binary loader ───────────────────────────────────────────────

bool AnimationPlayer::loadFromBinary(const uint8_t* data, size_t len) {
    if (len < 12) {
        LOGE("Animation binary is too small (%zu bytes)", len);
        return false;
    }
    if (data[0] != 'D' || data[1] != 'Z' || data[2] != 'A' || data[3] != 'N') {
        LOGE("Invalid animation binary magic");
        return false;
    }

    uint16_t version = 0;
    std::memcpy(&version, data + 4, sizeof(uint16_t));
    if (version != 1) {
        LOGE("Unsupported anim binary version %d", version);
        return false;
    }

    uint16_t numClips = 0;
    std::memcpy(&numClips, data + 6, sizeof(uint16_t));

    uint16_t numBones = 0;
    std::memcpy(&numBones, data + 8, sizeof(uint16_t));

    const uint8_t* p = data + 12;
    const uint8_t* end = data + len;

    clips_.resize(numClips);
    for (int c = 0; c < numClips; ++c) {
        if (p + 32 + 4 + 1 > end) {
            LOGE("Animation binary malformed: unexpected EOF at clip %d", c);
            clips_.clear();
            return false;
        }

        AnimClip& clip = clips_[c];
        clip.name = std::string((const char*)p, 32);
        // trim nulls
        size_t nul = clip.name.find('\0');
        if (nul != std::string::npos) clip.name.resize(nul);
        p += 32;

        std::memcpy(&clip.duration, p, sizeof(float));
        p += 4;

        uint8_t trackCount = *p;
        p += 1;

        clip.tracks.resize(trackCount);
        for (int t = 0; t < trackCount; ++t) {
            if (p + 32 + 2 > end) {
                LOGE("Animation binary malformed: unexpected EOF at track %d in clip %d", t, c);
                clips_.clear();
                return false;
            }

            AnimTrack& track = clip.tracks[t];
            track.boneName = std::string((const char*)p, 32);
            size_t n = track.boneName.find('\0');
            if (n != std::string::npos) track.boneName.resize(n);
            p += 32;

            uint16_t kfCount = 0;
            std::memcpy(&kfCount, p, sizeof(uint16_t));
            p += 2;

            if (p + kfCount * 36 > end) {
                LOGE("Animation binary malformed: unexpected EOF for keyframes at track %d in clip %d", t, c);
                clips_.clear();
                return false;
            }

            track.keyframes.resize(kfCount);
            for (int k = 0; k < kfCount; ++k) {
                AnimKeyframe& kf = track.keyframes[k];
                std::memcpy(&kf.time, p, sizeof(float)); p += 4;
                std::memcpy(&kf.pos[0], p, sizeof(float)); p += 4;
                std::memcpy(&kf.pos[1], p, sizeof(float)); p += 4;
                std::memcpy(&kf.pos[2], p, sizeof(float)); p += 4;
                std::memcpy(&kf.rot[0], p, sizeof(float)); p += 4;
                std::memcpy(&kf.rot[1], p, sizeof(float)); p += 4;
                std::memcpy(&kf.rot[2], p, sizeof(float)); p += 4;
                std::memcpy(&kf.rot[3], p, sizeof(float)); p += 4;
                p += 4; // padding
            }
        }
    }
    return true;
}

// ─── Math helpers ──────────────────────────────────────────────

void AnimationPlayer::lerpVec3(const float* a, const float* b, float t, float* out) {
    out[0] = a[0] + (b[0] - a[0]) * t;
    out[1] = a[1] + (b[1] - a[1]) * t;
    out[2] = a[2] + (b[2] - a[2]) * t;
}

void AnimationPlayer::slerp(const float* a, const float* b, float t, float* out) {
    float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
    float b0[4] = {b[0], b[1], b[2], b[3]};
    if (dot < 0.0f) {
        dot = -dot;
        b0[0] = -b0[0]; b0[1] = -b0[1]; b0[2] = -b0[2]; b0[3] = -b0[3];
    }
    if (dot > 0.9995f) {
        out[0] = a[0] + t*(b0[0]-a[0]);
        out[1] = a[1] + t*(b0[1]-a[1]);
        out[2] = a[2] + t*(b0[2]-a[2]);
        out[3] = a[3] + t*(b0[3]-a[3]);
        float len = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
        out[0] /= len; out[1] /= len; out[2] /= len; out[3] /= len;
        return;
    }
    float theta0 = std::acos(dot);
    float theta = theta0 * t;
    float sinTheta = std::sin(theta);
    float sinTheta0 = std::sin(theta0);
    float s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
    float s1 = sinTheta / sinTheta0;
    out[0] = a[0]*s0 + b0[0]*s1;
    out[1] = a[1]*s0 + b0[1]*s1;
    out[2] = a[2]*s0 + b0[2]*s1;
    out[3] = a[3]*s0 + b0[3]*s1;
}

void AnimationPlayer::quatToMat4(const float* q, float* m) {
    float xx = q[0]*q[0], yy = q[1]*q[1], zz = q[2]*q[2];
    float xy = q[0]*q[1], xz = q[0]*q[2], yz = q[1]*q[2];
    float wx = q[3]*q[0], wy = q[3]*q[1], wz = q[3]*q[2];
    m[0] = 1 - 2*(yy+zz); m[1] = 2*(xy-wz);   m[2] = 2*(xz+wy);   m[3] = 0;
    m[4] = 2*(xy+wz);   m[5] = 1 - 2*(xx+zz); m[6] = 2*(yz-wx);   m[7] = 0;
    m[8] = 2*(xz-wy);   m[9] = 2*(yz+wx);   m[10]= 1 - 2*(xx+yy); m[11]= 0;
    m[12]= 0;           m[13]= 0;           m[14]= 0;           m[15]= 1;
}

void AnimationPlayer::mat4Identity(float* m) {
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

void AnimationPlayer::mat4Mul(const float* a, const float* b, float* out) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out[col*4+row] = a[0*4+row]*b[col*4+0]
                           + a[1*4+row]*b[col*4+1]
                           + a[2*4+row]*b[col*4+2]
                           + a[3*4+row]*b[col*4+3];
        }
    }
}

// ─── Update / Play ───────────────────────────────────────────────

void AnimationPlayer::update(float dt) {
    time_ += dt;
    if (blend_ < 1.0f) {
        blend_ += dt / CROSSFADE_DURATION;
        if (blend_ > 1.0f) blend_ = 1.0f;
    }
}

void AnimationPlayer::play(uint8_t animId) {
    if (animId == current_) return;
    if (animId >= clips_.size()) animId = 0;
    previous_ = current_;
    current_ = animId;
    prevTime_ = time_;
    time_ = 0.0f;
    blend_ = 0.0f;
}

// ─── Evaluation ────────────────────────────────────────────────

int AnimationPlayer::findBoneIndex(const std::string& name) const {
    static const char* boneNames[] = {
        "player", "body", "middle",
        "left_thigh", "left_knee", "left_ankle",
        "right_thigh", "right_knee", "right_ankle",
        "left_shoulder", "left_elbow",
        "right_shoulder", "right_elbow",
        "head"
    };
    for (int i = 0; i < 14; ++i) {
        if (name == boneNames[i]) return i;
    }
    return -1;
}

void AnimationPlayer::evaluateClip(const AnimClip& clip, float t, float* outPos, float* outRot,
                                     const std::string& boneName) const {
    // Default
    outPos[0] = outPos[1] = outPos[2] = 0.0f;
    outRot[0] = outRot[1] = outRot[2] = 0.0f; outRot[3] = 1.0f;

    const AnimTrack* track = nullptr;
    for (const auto& tr : clip.tracks) {
        if (tr.boneName == boneName) { track = &tr; break; }
    }
    if (!track || track->keyframes.empty()) return;

    if (t <= track->keyframes.front().time) {
        std::memcpy(outPos, track->keyframes.front().pos, 3*sizeof(float));
        std::memcpy(outRot, track->keyframes.front().rot, 4*sizeof(float));
        return;
    }
    if (t >= track->keyframes.back().time) {
        std::memcpy(outPos, track->keyframes.back().pos, 3*sizeof(float));
        std::memcpy(outRot, track->keyframes.back().rot, 4*sizeof(float));
        return;
    }

    for (size_t i = 1; i < track->keyframes.size(); ++i) {
        if (t < track->keyframes[i].time) {
            const auto& a = track->keyframes[i-1];
            const auto& b = track->keyframes[i];
            float range = b.time - a.time;
            float alpha = (range > 0.0001f) ? (t - a.time) / range : 0.0f;
            lerpVec3(a.pos, b.pos, alpha, outPos);
            slerp(a.rot, b.rot, alpha, outRot);
            return;
        }
    }
}

int AnimationPlayer::evaluateState(uint8_t current, uint8_t previous, float blend, float time, float prevTime,
                                  float* boneMatrices, int maxBones) const {
    if (clips_.empty() || maxBones < 14) return 0;

    const AnimClip& curClip = clips_[current >= clips_.size() ? 0 : current];
    const AnimClip& prevClip = clips_[previous >= clips_.size() ? 0 : previous];

    float tCur = time;
    float tPrev = prevTime;
    if (isLooping(current)) {
        float dur = getDuration(current);
        if (dur > 0.0f) tCur = std::fmod(tCur, dur);
    }
    if (isLooping(previous)) {
        float dur = getDuration(previous);
        if (dur > 0.0f) tPrev = std::fmod(tPrev, dur);
    }

    static const char* boneNames[] = {
        "player", "body", "middle",
        "left_thigh", "left_knee", "left_ankle",
        "right_thigh", "right_knee", "right_ankle",
        "left_shoulder", "left_elbow",
        "right_shoulder", "right_elbow",
        "head"
    };

    for (int i = 0; i < 14; ++i) {
        float posA[3], rotA[4];
        float posB[3], rotB[4];
        evaluateClip(curClip, tCur, posA, rotA, boneNames[i]);
        evaluateClip(prevClip, tPrev, posB, rotB, boneNames[i]);

        float pos[3], rot[4];
        lerpVec3(posB, posA, blend, pos);
        slerp(rotB, rotA, blend, rot);

        float rM[16];
        quatToMat4(rot, rM);
        rM[12] = pos[0]; rM[13] = pos[1]; rM[14] = pos[2];
        std::memcpy(boneMatrices + i*16, rM, 16*sizeof(float));
    }
    return 14;
}

int AnimationPlayer::evaluate(float* boneMatrices, int maxBones) const {
    return evaluateState(current_, previous_, blend_, time_, prevTime_, boneMatrices, maxBones);
}

