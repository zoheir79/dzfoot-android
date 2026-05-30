#pragma once
#include "ARManager.h"
#include "Mesh.h"
#include "SceneGraph.h"
#include "Camera.h"
#include <GLES3/gl3.h>
#include <string>
#include <vector>

struct RigNode {
    std::string name;
    int32_t parentIndex = -1;
    int32_t boneIndex = -1; // 0..13 mapped from AnimationPlayer, or -1
    float localMatrix[16];
    Mesh staticMesh;
    bool hasMesh = false;
};

class PlayerRig {
public:
    std::vector<RigNode> nodes;

    bool load(const char* filename);
    void draw(const float* viewProj, const float* playerWorld, float rotY,
              uint8_t animId, uint8_t previousAnim, float blend, float time, float prevTime,
              GLuint shader, GLint mvpLoc, GLint colLoc, const float* teamColor);
    void destroy();
};

struct PlayerAnimState {
    uint8_t current = 0;
    uint8_t previous = 0;
    float blend = 1.0f;
    float time = 0.0f;
    float prevTime = 0.0f;

    void play(uint8_t animId) {
        if (animId == current) return;
        previous = current;
        current = animId;
        prevTime = time;
        time = 0.0f;
        blend = 0.0f;
    }

    void update(float dt) {
        time += dt;
        if (blend < 1.0f) {
            blend += dt / 0.2f; // CROSSFADE_DURATION
            if (blend > 1.0f) blend = 1.0f;
        }
    }
};

class ARRenderer {
public:
    void init();
    void destroy();

    void drawCameraBackground(ARManager& ar);
    void renderScene(ARManager& ar, const float* playerPositions, int numPlayers,
                     const float* ballPosition,
                     const float* boneMatrices = nullptr, int numBones = 0,
                     const uint8_t* playerAnims = nullptr, const float* playerVels = nullptr);

    void setPlayerMesh(const SkinnedMesh& mesh);

    SceneGraph& scene() { return scene_; }
    Camera& camera() { return camera_; }

private:
    GLuint cameraShader_  = 0;
    GLuint skinnedShader_ = 0; // Keeping for reference V2
    GLuint staticShader_  = 0; // Used for static objects AND rigid character parts
    GLuint quadVbo_       = 0;
    GLuint uvVbo_         = 0;

    SceneGraph scene_;
    Camera camera_;
    PlayerRig playerRig_;
    PlayerAnimState playerAnims_[22];

    // Pitch GLB half-extents in local space (meters), used to normalize grass lines
    float pitchHalf_[2] = { 52.5f, 34.0f };

    static constexpr float quadPositions_[8] = {
        -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,  1.0f,
    };

    void renderStaticObjects(const float* viewProj);
    void renderPlayers(const float* viewProj, const float* playerPositions, int numPlayers,
                       const uint8_t* playerAnims, const float* playerVels);
};
