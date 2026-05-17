#include "AssetLoader.h"

void AssetLoader::init(AAssetManager* mgr) {
    mgr_ = mgr;
}

std::vector<uint8_t> AssetLoader::load(const char* path) {
    std::vector<uint8_t> result;
    if (!mgr_) return result;
    AAsset* asset = AAssetManager_open(mgr_, path, AASSET_MODE_BUFFER);
    if (!asset) return result;
    const uint8_t* data = (const uint8_t*)AAsset_getBuffer(asset);
    int len = AAsset_getLength(asset);
    result.assign(data, data + len);
    AAsset_close(asset);
    return result;
}

bool AssetLoader::exists(const char* path) {
    if (!mgr_) return false;
    AAsset* asset = AAssetManager_open(mgr_, path, AASSET_MODE_STREAMING);
    if (asset) {
        AAsset_close(asset);
        return true;
    }
    return false;
}
 
