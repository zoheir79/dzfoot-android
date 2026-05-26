#pragma once
#include "ARManager.h"
#include "Mesh.h"
#include "SceneGraph.h"
#include "Camera.h"
#include <GLES3/gl3.h>

class ARRenderer {
public:
    void init();
    void destroy();

    void drawCameraBackground(ARManager& ar);
    void renderScene(ARManager& ar, const float* playerPositions, int numPlayers,
                     const float* boneMatrices = nullptr, int numBones = 0);

    void setPlayerMesh(const SkinnedMesh& mesh);

    SceneGraph& scene() { return scene_; }
    Camera& camera() { return camera_; }

private:
    GLuint cameraShader_  = 0;
    GLuint skinnedShader_ = 0;
    GLuint staticShader_  = 0;
    GLuint quadVbo_       = 0;
    GLuint uvVbo_         = 0;

    SceneGraph scene_;
    Camera camera_;

    static constexpr float quadPositions_[8] = {
        -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,  1.0f,
    };

    void renderStaticObjects(const float* viewProj);
    void renderPlayers(const float* viewProj, const float* playerPositions, int numPlayers,
                       const float* boneMatrices, int numBones);
};
