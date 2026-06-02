#include "DirectionalAnimBank.h"
#include <android/log.h>
#include <cmath>
#include <cstring>
#include <algorithm>

#define LOG_TAG "DirAnimBank"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#pragma pack(push, 1)
struct FileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t animCount;
};

struct FileMeta {
    uint8_t  category;
    uint8_t  angle;
    uint8_t  velIn;
    uint8_t  velOut;
    uint8_t  foot;
    uint8_t  flags;
    uint16_t frameCount;
    float    duration;
    uint32_t dataOffset;
};
#pragma pack(pop)

DirectionalAnimBank::DirectionalAnimBank() {
}

DirectionalAnimBank::~DirectionalAnimBank() {
    unload();
}

bool DirectionalAnimBank::load(AAssetManager* assetMgr, const char* assetPath) {
    unload();

    if (!assetMgr) {
        LOGE("AAssetManager is null!");
        return false;
    }

    AAsset* asset = AAssetManager_open(assetMgr, assetPath, AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Could not open asset: %s", assetPath);
        return false;
    }

    size_t size = AAsset_getLength(asset);
    fileBuffer_.resize(size);
    int readBytes = AAsset_read(asset, fileBuffer_.data(), size);
    AAsset_close(asset);

    if (readBytes < sizeof(FileHeader)) {
        LOGE("Asset is too small to contain header! readBytes=%d", readBytes);
        fileBuffer_.clear();
        return false;
    }

    const FileHeader* header = (const FileHeader*)fileBuffer_.data();
    if (header->magic != 0x4E415A44) { // 'DZAN'
        LOGE("Invalid magic in binary anim bank! magic=0x%08X", header->magic);
        fileBuffer_.clear();
        return false;
    }

    size_t minExpectedSize = sizeof(FileHeader) + header->animCount * sizeof(FileMeta);
    if (size < minExpectedSize) {
        LOGE("Invalid file size! size=%zu expected>=%zu", size, minExpectedSize);
        fileBuffer_.clear();
        return false;
    }

    const FileMeta* metaArr = (const FileMeta*)(fileBuffer_.data() + sizeof(FileHeader));
    clips_.reserve(header->animCount);

    for (int i = 0; i < header->animCount; ++i) {
        const FileMeta& m = metaArr[i];
        if (m.dataOffset + m.frameCount * 14 * sizeof(BoneFrame) > size) {
            LOGE("Clip %d data offset out of bounds!", i);
            unload();
            return false;
        }

        DirAnimClip clip;
        clip.category = m.category;
        clip.angleQuant = m.angle;
        clip.velocityIn = m.velIn;
        clip.velocityOut = m.velOut;
        clip.foot = m.foot;
        clip.flags = m.flags;
        clip.frameCount = m.frameCount;
        clip.duration = m.duration;
        clip.frames = (const BoneFrame*)(fileBuffer_.data() + m.dataOffset);

        clips_.push_back(clip);

        if (clip.category < 17) {
            categoryIndex_[clip.category].push_back(i);
        }
    }

    LOGI("Loaded DirectionalAnimBank successfully: %zu clips from %s (%zu bytes)",
         clips_.size(), assetPath, size);
    return true;
}

void DirectionalAnimBank::unload() {
    clips_.clear();
    fileBuffer_.clear();
    for (int i = 0; i < 17; ++i) {
        categoryIndex_[i].clear();
    }
}

const DirAnimClip* DirectionalAnimBank::select(const Query& q) const {
    if (clips_.empty()) return nullptr;

    uint8_t cat = q.category;
    if (cat >= 17) cat = 0; // fallback to idle

    const std::vector<int>& indices = categoryIndex_[cat];
    if (indices.empty()) {
        // Fallback: search for any category index that matches
        return nullptr;
    }

    // Convert relative angle [0, 180] deg to quantified:
    // 0 = 0° (0-22.5)
    // 1 = 45° (22.5-67.5)
    // 2 = 90° (67.5-112.5)
    // 3 = 135° (112.5-157.5)
    // 4 = 180° (157.5-180)
    uint8_t queryAngleQuant = 0;
    if (q.relAngleDeg < 22.5f) {
        queryAngleQuant = 0;
    } else if (q.relAngleDeg < 67.5f) {
        queryAngleQuant = 1;
    } else if (q.relAngleDeg < 112.5f) {
        queryAngleQuant = 2;
    } else if (q.relAngleDeg < 157.5f) {
        queryAngleQuant = 3;
    } else {
        queryAngleQuant = 4;
    }

    const DirAnimClip* bestClip = nullptr;
    int bestScore = -99999;

    for (int idx : indices) {
        const DirAnimClip& clip = clips_[idx];

        // Score this clip vs query
        int score = 0;

        // VeloMatch: exact velocityIn match is great (+100)
        // close match (+50)
        if (clip.velocityIn == q.velocityIn) {
            score += 100;
        } else if (std::abs(clip.velocityIn - q.velocityIn) <= 1) {
            score += 50;
        } else {
            score -= 50;
        }

        // AngleMatch: exact quantified angle is fantastic (+200)
        // adjacent angle (+100)
        if (clip.angleQuant == queryAngleQuant) {
            score += 200;
        } else {
            int dist = std::abs(clip.angleQuant - queryAngleQuant);
            score += (10 - dist) * 20; // smaller distance is better
        }

        // Prefer base anims (flags & 1) over transitions (+50)
        if (clip.flags & 1) {
            score += 50;
        }

        if (score > bestScore) {
            bestScore = score;
            bestClip = &clip;
        }
    }

    return bestClip;
}
