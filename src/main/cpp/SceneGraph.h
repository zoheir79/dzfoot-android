#pragma once
#include <vector>
#include <string>
#include <memory>
#include <GLES3/gl3.h>
#include "Mesh.h"

struct Transform {
    float position[3] = {0,0,0};
    float rotation[4] = {0,0,0,1}; // quaternion x,y,z,w
    float scale[3]    = {1,1,1};

    void toMatrix(float* out) const;
    static void quatToMat4(const float* q, float* m);
    static void mat4Mul(const float* a, const float* b, float* out);
};

class SceneNode {
public:
    std::string name;
    Transform local;
    float worldMatrix[16]; // column-major 4x4
    int parentIndex = -1;
    std::vector<int> children;

    // Renderable data
    SkinnedMesh skinnedMesh;
    Mesh staticMesh;
    bool useSkinning = false;
    bool visible = true;

    void updateWorld(const float* parentWorld);
};

class SceneGraph {
public:
    std::vector<SceneNode> nodes;

    int findNode(const std::string& name) const;
    int addNode(const std::string& name, int parent = -1);
    void update();

    void drawAll(GLuint shader, const float* viewProj, const float* boneMatrices = nullptr, int numBones = 0) const;
};
