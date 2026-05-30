#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include "Mesh.h"

// Minimal GLB loader for GameplayFootball exported assets
// Parses embedded GLTF JSON + BIN chunk, extracts mesh + skin + bones

struct GLBNode {
    std::string name;
    float localMatrix[16];
    int32_t parentIndex = -1;
    int32_t skinIndex = -1;
    int32_t meshIndex = -1;
    std::vector<int32_t> childrenIndices;
};

struct GLBSkin {
    std::string name;
    std::vector<int32_t> jointIndices; // indices into nodes
    std::vector<float> inverseBindMatrices; // 16 floats per joint
};

struct GLBPrimitive {
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    std::string materialName;
};

struct GLBMesh {
    std::string name;
    std::vector<GLBPrimitive> primitives;
};

struct GLBScene {
    std::vector<GLBNode> nodes;
    std::vector<GLBSkin> skins;
    std::vector<GLBMesh> meshes;
    int32_t sceneRootNode = -1;
};

class GLBLoader {
public:
    bool load(const uint8_t* data, size_t len, GLBScene& outScene);
    bool loadFromAsset(const char* path, GLBScene& outScene);

private:
    bool parseGLB(const uint8_t* data, size_t len, GLBScene& outScene);
};
