#pragma once
#include <android/asset_manager.h>
#include <vector>
#include <string>

class AssetLoader {
public:
    void init(AAssetManager* mgr);
    std::vector<uint8_t> load(const char* path);
    bool exists(const char* path);

private:
    AAssetManager* mgr_ = nullptr;
};
