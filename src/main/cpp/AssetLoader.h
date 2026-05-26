#pragma once
#include <android/asset_manager.h>
#include <vector>
#include <string>

class AssetLoader {
public:
    void init(AAssetManager* mgr);
    std::vector<uint8_t> load(const char* path);
    bool exists(const char* path);

    // Static helper for one-shot loading without instance
    static std::vector<uint8_t> loadAsBytes(AAssetManager* mgr, const char* path);

private:
    AAssetManager* mgr_ = nullptr;
};
