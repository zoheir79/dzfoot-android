#pragma once
#include "ARManager.h"
#include "Mesh.h"
#include "GLBLoader.h"
#include "SceneGraph.h"
#include "Camera.h"
#include "DirectionalAnimBank.h"
#include <GLES3/gl3.h>
#include <string>
#include <vector>

struct MeshPart {
    SkinnedMesh mesh;
    int materialIndex = -1;
    std::string materialName;
    float baseColor[4] = {1,1,1,1};
};

struct RigNode {
    std::string name;
    int32_t parentIndex = -1;
    int32_t boneIndex = -1; // 0..13 mapped from AnimationPlayer, or -1
    float localMatrix[16];
    // Bind-pose TRS, used to recompose local transform when animation overrides a channel
    float bindT[3] = {0.0f, 0.0f, 0.0f};
    float bindR[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float bindS[3] = {1.0f, 1.0f, 1.0f};
    std::vector<MeshPart> staticMeshes;
};

class PlayerRig {
public:
    std::vector<RigNode> nodes;
    std::vector<GLBAnimation> animations; // embedded GLB clips
    GLBSkin skin;
    bool hasSkin = false;
    GLuint skinTex = 0;
    GLuint kitTex = 0;
    GLuint shoeTex = 0;
    GLuint shortTex = 0;
    GLuint defaultSkinTex = 0;

    // Persistent scratch buffers (resized once) to eliminate per-frame allocations
    std::vector<float> scratchT, scratchR, scratchS;
    std::vector<float> scratchCurT, scratchCurR, scratchCurS;
    std::vector<float> scratchPrevT, scratchPrevR, scratchPrevS;
    std::vector<float> scratchGlobalMats;

    bool load(const char* filename);
    void draw(const float* viewProj, const float* playerWorld, float rotY,
              uint8_t animId, uint8_t previousAnim, float blend, float time, float prevTime,
              GLuint staticShader, GLuint skinnedShader, const float* teamColor,
              int playerIndex = -1, const DirAnimClip* dirClip = nullptr);
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
                     const uint8_t* playerAnims = nullptr, const float* playerVels = nullptr,
                     const float* playerRotY = nullptr,
                     const uint8_t* playerFlags = nullptr, const uint8_t* playerTeams = nullptr,
                     const uint8_t* playerRoles = nullptr);

    void setPlayerMesh(const SkinnedMesh& mesh);

    SceneGraph& scene() { return scene_; }
    Camera& camera() { return camera_; }

    // Scale factors derived from actual pitch GLB half-extents so players align
    // with the visible field white lines regardless of pitch asset size.
    float getPitchScaleX() const { return pitchHalf_[0] * 0.1f; }
    float getPitchScaleZ() const { return pitchHalf_[1] * 0.1f / 0.4306f; } // exact GF pitchHalfH/Y_FIELD_SCALE = 36/83.6

private:
    GLuint cameraShader_  = 0;
    GLuint skinnedShader_ = 0; // Keeping for reference V2
    GLuint staticShader_  = 0; // Used for static objects AND rigid character parts
    GLuint quadVbo_       = 0;
    GLuint uvVbo_         = 0;

    SceneGraph scene_;
    Camera camera_;
    PlayerRig playerRig_;
    DirectionalAnimBank dirAnimBank_;
    PlayerAnimState playerAnims_[22];
    GLuint pitchTex_ = 0;
    GLuint ballTex_ = 0;
    GLuint stadiumTex_ = 0;
    GLuint crowdTex_ = 0;
    GLuint goalnettingTex_ = 0;
    GLuint pitchOverlayTex_ = 0;

    // Pitch GLB half-extents in local space (meters), used to normalize grass lines
    float pitchHalf_[2] = { 52.5f, 34.0f };

    static constexpr float quadPositions_[8] = {
        -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,  1.0f,
    };

    void renderStaticObjects(const float* viewProj);
    void renderPlayers(const float* viewProj, const float* playerPositions, int numPlayers,
                       const uint8_t* playerAnims, const float* playerVels,
                       const float* playerRotY,
                       const uint8_t* playerFlags, const uint8_t* playerTeams,
                       const uint8_t* playerRoles);
};
