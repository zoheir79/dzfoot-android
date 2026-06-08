#pragma once
#include "ARManager.h"
#include "Mesh.h"
#include "GLBLoader.h"
#include "SceneGraph.h"
#include "Camera.h"
#include "DirectionalAnimBank.h"
#include "protocol/DZFootProtocol.h"
#include <GLES3/gl3.h>
#include <cmath>
#include <string>
#include <vector>

struct MeshPart {
    SkinnedMesh mesh;
    int materialIndex = -1;
    std::string materialName;
    float baseColor[4] = {1,1,1,1};
    int parentNodeIndex = -1; // for attached modular parts (hair/beard parented to neck/head)
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

struct AvatarConfig {
    uint8_t bodyType = 1;     // 0=thin, 1=average, 2=muscular, 3=heavy
    uint8_t hairStyle = 0;    // 0=short, 1=long, 2=mohawk, 3=curly, 4=ponytail, 5=bald
    uint8_t beardStyle = 0;   // 0=none, 1=stubble, 2=short, 3=full
    uint8_t skinColor = 3;    // 0..6 (7 tones)
    uint8_t hairColor = 0;    // 0..7 (8 colors)
    uint8_t playerNumber = 0; // jersey number 0..99
    float   height = 1.0f;
};

class PlayerRig {
public:
    std::vector<RigNode> nodes;
    std::vector<GLBAnimation> animations; // embedded GLB clips
    GLBSkin skin;
    bool hasSkin = false;
    GLuint skinTex = 0;
    GLuint skinTexs[7] = {};
    GLuint hairTexs[8] = {};   // runtime-swapped hair color textures
    GLuint kitTex = 0;
    GLuint shoeTex = 0;
    GLuint shortTex = 0;
    GLuint defaultSkinTex = 0;

    // Modular attachments (hair/beard meshes parented to head node)
    std::vector<MeshPart> attachedMeshes;

    // Persistent scratch buffers (resized once) to eliminate per-frame allocations
    std::vector<float> scratchT, scratchR, scratchS;
    std::vector<float> scratchCurT, scratchCurR, scratchCurS;
    std::vector<float> scratchPrevT, scratchPrevR, scratchPrevS;
    std::vector<float> scratchGlobalMats;

    bool load(const char* filename);
    // Modular composition: body + hair + beard loaded separately
    bool loadModular(const AvatarConfig& cfg);
    bool attachPart(const char* partGlb, const char* parentBoneName, const char* materialCat);
    void draw(const float* viewProj, const float* playerWorld, float rotY,
              uint8_t animId, uint8_t previousAnim, float blend, float time, float prevTime,
              GLuint staticShader, GLuint skinnedShader, const float* teamColor,
              GLuint kitTexture, GLuint shortTexture,
              int playerIndex = -1, const DirAnimClip* dirClip = nullptr,
              const AvatarConfig* avatar = nullptr,
              const float* lightSpaceMatrix = nullptr, GLuint shadowTex = 0);
    void destroy();

    AvatarConfig loadedCfg_;
    bool hasLoadedCfg_ = false;

    bool configMatches(const AvatarConfig& cfg) const {
        return hasLoadedCfg_ &&
               loadedCfg_.bodyType == cfg.bodyType &&
               loadedCfg_.hairStyle == cfg.hairStyle &&
               loadedCfg_.beardStyle == cfg.beardStyle &&
               loadedCfg_.skinColor == cfg.skinColor &&
               loadedCfg_.hairColor == cfg.hairColor &&
               loadedCfg_.playerNumber == cfg.playerNumber &&
               fabsf(loadedCfg_.height - cfg.height) < 0.001f;
    }

private:
    int findNodeIndex(const char* name) const;
    bool loadBody(const char* bodyGlb);
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
                     const uint8_t* playerRoles = nullptr,
                     class TouchController* ctrl = nullptr, int screenW = 0, int screenH = 0);

    void setPlayerMesh(const SkinnedMesh& mesh);
    void setMatchSetup(const dzfoot::MatchSetupPacket& setup);

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
    GLuint uiShader_      = 0; // 2D on-screen controller overlay
    GLuint quadVbo_       = 0;
    GLuint uvVbo_         = 0;
    GLuint uiVbo_         = 0; // dynamic buffer for UI triangles

    SceneGraph scene_;
    Camera camera_;
    PlayerRig playerRigs_[25];  // one modular rig per entity (22 players + 3 officials)
    DirectionalAnimBank dirAnimBank_;
    PlayerAnimState playerAnims_[25];
    dzfoot::MatchSetupPacket setup_;
    bool hasSetup_ = false;
    GLuint pitchTex_ = 0;
    GLuint ballTex_ = 0;
    GLuint stadiumTex_ = 0;
    GLuint crowdTex_ = 0;
    GLuint goalnettingTex_ = 0;
    GLuint pitchOverlayTex_ = 0;
    // Per-team kit textures generated from MatchSetup colors
    GLuint teamKitTex_[2] = {0, 0};
    GLuint teamShortTex_[2] = {0, 0};

    // Pitch GLB half-extents in local space (meters), used to normalize grass lines
    float pitchHalf_[2] = { 52.5f, 34.0f };

    static constexpr float quadPositions_[8] = {
        -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,  1.0f,
    };

    void renderStaticObjects(const float* viewProj, const float* lightSpaceMatrix);
    void renderPlayers(const float* viewProj, const float* lightSpaceMatrix,
                       const float* playerPositions, int numPlayers,
                       const uint8_t* playerAnims, const float* playerVels,
                       const float* playerRotY,
                       const uint8_t* playerFlags, const uint8_t* playerTeams,
                       const uint8_t* playerRoles);

    void renderShadowMap(const float* playerPositions, int numPlayers,
                         const uint8_t* playerAnims, const float* playerVels,
                         const float* playerRotY,
                         const uint8_t* playerFlags, const uint8_t* playerTeams,
                         const uint8_t* playerRoles);

    // Draw on-screen controller: joystick, action buttons, radar
    void renderUI(class TouchController& ctrl, int screenW, int screenH,
                  const float* viewProj, const float* playerPositions,
                  const uint8_t* playerFlags, const uint8_t* playerTeams);

    GLuint shadowFbo_ = 0;
    GLuint shadowTex_ = 0;
    GLuint shadowShader_ = 0;
    static constexpr int kShadowMapSize = 2048;
    float lightSpaceMatrix_[16];
};
