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
    // Bind-pose TRS (decomposed) so animation can override individual channels
    float bindT[3] = {0.0f, 0.0f, 0.0f};
    float bindR[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // x,y,z,w
    float bindS[3] = {1.0f, 1.0f, 1.0f};
    int32_t parentIndex = -1;
    int32_t skinIndex = -1;
    int32_t meshIndex = -1;
    std::vector<int32_t> childrenIndices;
};

// glTF animation data (embedded clips)
struct GLBAnimSampler {
    std::vector<float> input;   // keyframe times (seconds)
    std::vector<float> output;  // flattened values (3 per key for T/S, 4 for R)
    int components = 4;         // 3 or 4
    int interpolation = 0;      // 0=LINEAR, 1=STEP, 2=CUBICSPLINE(treated as LINEAR)
};

struct GLBAnimChannel {
    int targetNode = -1;
    int path = 0;   // 0=translation, 1=rotation, 2=scale
    int sampler = -1;
};

struct GLBAnimation {
    std::string name;
    std::vector<GLBAnimChannel> channels;
    std::vector<GLBAnimSampler> samplers;
    float duration = 0.0f;
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
    int materialIndex = -1;
};

struct GLBMesh {
    std::string name;
    std::vector<GLBPrimitive> primitives;
};

struct GLBMaterial {
    std::string name;
    float baseColor[4] = {1,1,1,1};
};

struct GLBScene {
    std::vector<GLBNode> nodes;
    std::vector<GLBSkin> skins;
    std::vector<GLBMesh> meshes;
    std::vector<GLBMaterial> materials;
    std::vector<GLBAnimation> animations;
    int32_t sceneRootNode = -1;
};

class GLBLoader {
public:
    bool load(const uint8_t* data, size_t len, GLBScene& outScene);
    bool loadFromAsset(const char* path, GLBScene& outScene);

private:
    bool parseGLB(const uint8_t* data, size_t len, GLBScene& outScene);
};
