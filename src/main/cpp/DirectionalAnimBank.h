#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <android/asset_manager.h>

#pragma pack(push, 1)
struct BoneFrame {
    float rotation[4]; // quaternion xyzw
    float position[3]; // translation xyz
};
#pragma pack(pop)

struct DirAnimClip {
    uint8_t  category;
    uint8_t  angleQuant;    // 0=0°, 1=45°, 2=90°, 3=135°, 4=180°
    uint8_t  velocityIn;     // 0=idle, 1=walk, 2=dribble, 3=sprint
    uint8_t  velocityOut;
    uint8_t  foot;           // 0=right, 1=left
    uint8_t  flags;          // bit0=baseanim, bit1=transition
    uint16_t frameCount;
    float    duration;
    const BoneFrame* frames; // frameCount * 14 bones
};

class DirectionalAnimBank {
public:
    DirectionalAnimBank();
    ~DirectionalAnimBank();

    bool load(AAssetManager* assetMgr, const char* assetPath);
    void unload();

    struct Query {
        uint8_t category;
        float   relAngleDeg;    // relative angle [0, 180] deg
        uint8_t velocityIn;     // speed-derived velocity type (0=idle, 1=walk, 2=dribble, 3=sprint)
    };

    const DirAnimClip* select(const Query& q) const;

    bool empty() const { return clips_.empty(); }
    size_t size() const { return clips_.size(); }

private:
    std::vector<DirAnimClip> clips_;
    std::vector<uint8_t> fileBuffer_; // holds raw file bytes in memory
    std::vector<int> categoryIndex_[17]; // indices of clips by category
};
