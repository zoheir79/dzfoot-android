#include "ARRenderer.h"
#include "Shader.h"
#include "Mesh.h"
#include "GLBLoader.h"
#include "AssetLoader.h"
#include <android/log.h>
#include <cmath>
#include <cstring>

#define LOG_TAG "ARRenderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

extern AAssetManager* gAssetManager;

static bool loadStaticGLB(const char* filename, Mesh& outMesh, float* outHalfXZ = nullptr) {
    if (!gAssetManager) return false;
    std::vector<uint8_t> bytes = AssetLoader::loadAsBytes(gAssetManager, filename);
    if (bytes.empty()) {
        LOGE("Failed to read asset: %s", filename);
        return false;
    }

    GLBLoader loader;
    GLBScene scene;
    if (!loader.load(bytes.data(), bytes.size(), scene)) {
        LOGE("Failed to parse GLB: %s", filename);
        return false;
    }

    std::vector<Vertex> allVerts;
    std::vector<uint32_t> allIndices;

    for (const auto& mesh : scene.meshes) {
        for (const auto& prim : mesh.primitives) {
            uint32_t indexOffset = static_cast<uint32_t>(allVerts.size());
            for (const auto& sv : prim.vertices) {
                Vertex v;
                std::memcpy(v.pos, sv.pos, sizeof(float) * 3);
                std::memcpy(v.normal, sv.normal, sizeof(float) * 3);
                std::memcpy(v.uv, sv.uv, sizeof(float) * 2);
                allVerts.push_back(v);
            }
            for (auto idx : prim.indices) {
                allIndices.push_back(idx + indexOffset);
            }
        }
    }

    if (!allVerts.empty()) {
        float minP[3] = { 1e9f, 1e9f, 1e9f };
        float maxP[3] = { -1e9f, -1e9f, -1e9f };
        for (const auto& v : allVerts) {
            for (int k = 0; k < 3; ++k) {
                if (v.pos[k] < minP[k]) minP[k] = v.pos[k];
                if (v.pos[k] > maxP[k]) maxP[k] = v.pos[k];
            }
        }
        LOGI("Static GLB '%s': verts=%zu bbox=[%.2f,%.2f,%.2f]..[%.2f,%.2f,%.2f]",
             filename, allVerts.size(), minP[0], minP[1], minP[2], maxP[0], maxP[1], maxP[2]);
        if (outHalfXZ) {
            outHalfXZ[0] = 0.5f * (maxP[0] - minP[0]);
            outHalfXZ[1] = 0.5f * (maxP[2] - minP[2]);
        }
        outMesh.upload(allVerts, allIndices);
        return true;
    }
    return false;
}

static bool loadSkinnedGLB(const char* filename, SkinnedMesh& outMesh) {
    if (!gAssetManager) return false;
    std::vector<uint8_t> bytes = AssetLoader::loadAsBytes(gAssetManager, filename);
    if (bytes.empty()) {
        LOGE("Failed to read asset: %s", filename);
        return false;
    }

    GLBLoader loader;
    GLBScene scene;
    if (!loader.load(bytes.data(), bytes.size(), scene)) {
        LOGE("Failed to parse GLB: %s", filename);
        return false;
    }

    std::vector<SkinnedVertex> allVerts;
    std::vector<uint32_t> allIndices;

    for (const auto& mesh : scene.meshes) {
        for (const auto& prim : mesh.primitives) {
            uint32_t indexOffset = static_cast<uint32_t>(allVerts.size());
            for (const auto& sv : prim.vertices) {
                allVerts.push_back(sv);
            }
            for (auto idx : prim.indices) {
                allIndices.push_back(idx + indexOffset);
            }
        }
    }

    if (!allVerts.empty()) {
        // Diagnostics: compute bounding box and check bone weights
        float minP[3] = { 1e9f, 1e9f, 1e9f };
        float maxP[3] = { -1e9f, -1e9f, -1e9f };
        float maxWeightSum = 0.0f;
        for (const auto& v : allVerts) {
            for (int k = 0; k < 3; ++k) {
                if (v.pos[k] < minP[k]) minP[k] = v.pos[k];
                if (v.pos[k] > maxP[k]) maxP[k] = v.pos[k];
            }
            float ws = v.boneWeights[0] + v.boneWeights[1] + v.boneWeights[2] + v.boneWeights[3];
            if (ws > maxWeightSum) maxWeightSum = ws;
        }
        LOGI("Skinned GLB '%s': verts=%zu bbox=[%.2f,%.2f,%.2f]..[%.2f,%.2f,%.2f] maxWeightSum=%.3f",
             filename, allVerts.size(), minP[0], minP[1], minP[2], maxP[0], maxP[1], maxP[2], maxWeightSum);

        outMesh.upload(allVerts, allIndices);
        return true;
    }
    return false;
}

static void loadFallbackSkinnedPlayer(SkinnedMesh& outMesh) {
    std::vector<SkinnedVertex> verts;
    std::vector<uint32_t> indices;

    float h = 0.25f;
    auto addFace = [&](float nx, float ny, float nz, 
                       float x1, float y1, float z1,
                       float x2, float y2, float z2, 
                       float x3, float y3, float z3,
                       float x4, float y4, float z4) {
        uint32_t offset = static_cast<uint32_t>(verts.size());
        SkinnedVertex v1 = {{x1,y1,z1}, {nx,ny,nz}, {0,0}, {0,0,0,0}, {1,0,0,0}};
        SkinnedVertex v2 = {{x2,y2,z2}, {nx,ny,nz}, {1,0}, {0,0,0,0}, {1,0,0,0}};
        SkinnedVertex v3 = {{x3,y3,z3}, {nx,ny,nz}, {1,1}, {0,0,0,0}, {1,0,0,0}};
        SkinnedVertex v4 = {{x4,y4,z4}, {nx,ny,nz}, {0,1}, {0,0,0,0}, {1,0,0,0}};
        verts.push_back(v1); verts.push_back(v2); verts.push_back(v3); verts.push_back(v4);
        indices.push_back(offset + 0); indices.push_back(offset + 1); indices.push_back(offset + 2);
        indices.push_back(offset + 0); indices.push_back(offset + 2); indices.push_back(offset + 3);
    };

    addFace(0,0,1, -h,-h,h, h,-h,h, h,h,h, -h,h,h);
    addFace(0,0,-1, h,-h,-h, -h,-h,-h, -h,h,-h, h,h,-h);
    addFace(-1,0,0, -h,-h,-h, -h,-h,h, -h,h,h, -h,h,-h);
    addFace(1,0,0, h,-h,h, h,-h,-h, h,h,-h, h,h,h);
    addFace(0,1,0, -h,h,h, h,h,h, h,h,-h, -h,h,-h);
    addFace(0,-1,0, -h,-h,-h, h,-h,-h, h,-h,h, -h,-h,h);

    outMesh.upload(verts, indices);
}

// ─── OpenGL ES 3.0 Shaders ────────────────────────────────────────

static const char* CAMERA_VERT = R"(#version 300 es
in vec2 a_Position;
in vec2 a_TexCoord;
out vec2 v_TexCoord;
void main() {
    gl_Position = vec4(a_Position, 0.0, 1.0);
    v_TexCoord = a_TexCoord;
}
)";

static const char* CAMERA_FRAG = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
in vec2 v_TexCoord;
out vec4 outColor;
uniform samplerExternalOES u_Texture;
void main() {
    outColor = texture(u_Texture, v_TexCoord);
}
)";

static const char* SKINNED_VERT = R"(#version 300 es
precision mediump float;
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in uvec4 a_BoneIndices;
layout(location = 4) in vec4 a_BoneWeights;

uniform mat4 u_ModelViewProj;
uniform mat4 u_BoneMatrices[32];

out vec3 v_Normal;
out vec2 v_TexCoord;
out vec3 v_WorldPos;

void main() {
    float weightSum = a_BoneWeights.x + a_BoneWeights.y + a_BoneWeights.z + a_BoneWeights.w;

    vec4 skinnedPos;
    vec3 skinnedNormal;
    if (weightSum < 0.001) {
        // Degenerate/unloaded weights: fall back to bind pose so the mesh stays intact
        skinnedPos = vec4(a_Position, 1.0);
        skinnedNormal = a_Normal;
    } else {
        mat4 skinMatrix =
            u_BoneMatrices[a_BoneIndices.x] * a_BoneWeights.x +
            u_BoneMatrices[a_BoneIndices.y] * a_BoneWeights.y +
            u_BoneMatrices[a_BoneIndices.z] * a_BoneWeights.z +
            u_BoneMatrices[a_BoneIndices.w] * a_BoneWeights.w;
        skinnedPos = skinMatrix * vec4(a_Position, 1.0);
        skinnedNormal = (skinMatrix * vec4(a_Normal, 0.0)).xyz;
    }

    v_Normal = normalize(skinnedNormal);
    v_TexCoord = a_TexCoord;
    v_WorldPos = skinnedPos.xyz;
    gl_Position = u_ModelViewProj * skinnedPos;
}
)";

static const char* SKINNED_FRAG = R"(#version 300 es
precision mediump float;
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_WorldPos;
out vec4 outColor;
uniform vec3 u_Color;
void main() {
    vec3 N = normalize(v_Normal);
    vec3 L1 = normalize(vec3(0.3, 1.0, 0.4));  // main sun light
    vec3 L2 = normalize(vec3(-0.5, 0.5, -0.3)); // fill light
    float diff1 = max(dot(N, L1), 0.0);
    float diff2 = max(dot(N, L2), 0.0) * 0.4;
    float amb = 0.4;
    // Specular highlight for kit
    vec3 V = normalize(-v_WorldPos);
    vec3 R = reflect(-L1, N);
    float spec = pow(max(dot(V, R), 0.0), 16.0) * 0.3;
    vec3 finalColor = u_Color * (diff1 + diff2 + amb) + vec3(spec);
    outColor = vec4(finalColor, 1.0);
}
)";

static const char* STATIC_VERT = R"(#version 300 es
precision mediump float;
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
uniform mat4 u_ModelViewProj;
out vec3 v_Normal;
out vec2 v_TexCoord;
out vec3 v_LocalPos;
void main() {
    v_Normal = a_Normal;
    v_TexCoord = a_TexCoord;
    v_LocalPos = a_Position;
    gl_Position = u_ModelViewProj * vec4(a_Position, 1.0);
}
)";

static const char* STATIC_FRAG = R"(#version 300 es
precision mediump float;
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_LocalPos;
out vec4 outColor;
uniform vec3 u_Color;
uniform int u_MaterialType; // 0=default, 1=pitch, 2=goal, 3=ball
uniform vec2 u_PitchHalf;   // pitch half-extents in local space (meters)

vec3 grassPitch() {
    vec3 baseGreen = vec3(0.20, 0.56, 0.16);
    vec3 darkGreen = vec3(0.16, 0.46, 0.13);
    vec3 white     = vec3(0.92, 0.94, 0.90);

    // Normalize local position into [-1, 1] across the pitch (robust to GLB units)
    vec2 n = v_LocalPos.xz / max(u_PitchHalf, vec2(0.001));

    // Mowing stripes: ~14 bands along the length, soft contrast
    float stripe = step(0.5, fract(n.x * 7.0));
    vec3 grass = mix(baseGreen, darkGreen, stripe);

    // Line thickness in normalized units
    float tx = 0.012;
    float tz = 0.018;

    // Outer boundary (just inside the edge)
    float sideX = step(0.96, abs(n.x)) * step(abs(n.x), 0.99);
    float sideZ = step(0.96, abs(n.y)) * step(abs(n.y), 0.99);

    // Halfway line (x = 0)
    float halfway = step(abs(n.x), tx);

    // Center circle (radius ~0.17 of half-length)
    float d = length(vec2(n.x, n.y * (u_PitchHalf.y / u_PitchHalf.x)));
    float circle = step(0.16, d) * step(d, 0.16 + tx);
    // Center spot
    float spot = step(d, 0.025);

    // Penalty boxes (both ends): box spans |x| in [0.82,1.0], |z|<0.55
    float boxX = (step(0.82, abs(n.x)) * step(abs(n.x), 0.82 + tx)) * step(abs(n.y), 0.55);
    float boxZin = step(abs(abs(n.y) - 0.55), tz) * step(0.82, abs(n.x));
    float penalty = clamp(boxX + boxZin, 0.0, 1.0);

    float lineMask = clamp(sideX + sideZ + halfway + circle + spot + penalty, 0.0, 1.0);
    return mix(grass, white, lineMask);
}

vec3 ballPattern() {
    // Robust 3D soccer ball pattern using local positions on the sphere (independent of UVs)
    float pattern = step(0.15, sin(v_LocalPos.x * 12.0) * sin(v_LocalPos.y * 12.0) * sin(v_LocalPos.z * 12.0));
    return mix(vec3(0.98, 0.98, 0.98), vec3(0.08, 0.08, 0.08), pattern);
}

void main() {
    vec3 N = normalize(v_Normal);
    vec3 L1 = normalize(vec3(0.3, 1.0, 0.4));
    vec3 L2 = normalize(vec3(-0.5, 0.5, -0.3));
    float diff1 = max(dot(N, L1), 0.0);
    float diff2 = max(dot(N, L2), 0.0) * 0.3;
    float amb = 0.45;
    float light = diff1 + diff2 + amb;
    
    vec3 baseColor;
    if (u_MaterialType == 1) {
        baseColor = grassPitch();
    } else if (u_MaterialType == 3) {
        baseColor = ballPattern();
    } else {
        baseColor = u_Color;
    }
    
    outColor = vec4(baseColor * light, 1.0);
}
)";

// ─── Matrix utilities ────────────────────────────────────────────

static void mat4Mul(const float* a, const float* b, float* out) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out[col*4+row] = a[0*4+row]*b[col*4+0]
                           + a[1*4+row]*b[col*4+1]
                           + a[2*4+row]*b[col*4+2]
                           + a[3*4+row]*b[col*4+3];
        }
    }
}

static void mat4Identity(float* m) {
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

static void mat4Translate(float* m, float x, float y, float z) {
    mat4Identity(m);
    m[12] = x; m[13] = y; m[14] = z;
}

static void mat4Scale(float* m, float x, float y, float z) {
    mat4Identity(m);
    m[0] = x; m[5] = y; m[10] = z;
}

// ─── ARRenderer implementation ───────────────────────────────────

void ARRenderer::init() {
    cameraShader_ = Shader::compile(CAMERA_VERT, CAMERA_FRAG);
    skinnedShader_ = Shader::compile(SKINNED_VERT, SKINNED_FRAG);
    staticShader_ = Shader::compile(STATIC_VERT, STATIC_FRAG);

    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadPositions_), quadPositions_, GL_STATIC_DRAW);

    static const float uvs[8] = {0, 1, 1, 1, 0, 0, 1, 0};
    glGenBuffers(1, &uvVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, uvVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(uvs), uvs, GL_STATIC_DRAW);

    // Build static scene graph
    int root = scene_.addNode("root", -1);
    
    // 1. Terrain (pitch)
    int pitch = scene_.addNode("pitch", root);
    if (!loadStaticGLB("pitch.glb", scene_.nodes[pitch].staticMesh, pitchHalf_)) {
        LOGE("Could not load pitch.glb, falling back to loadCube");
        scene_.nodes[pitch].staticMesh.loadCube(1.0f);
        scene_.nodes[pitch].local.scale[0] = 11.0f;
        scene_.nodes[pitch].local.scale[1] = 0.25f;
        scene_.nodes[pitch].local.scale[2] = 5.0f;
        scene_.nodes[pitch].local.position[1] = -0.1f;
    } else {
        LOGI("Pitch half-extents (local meters): X=%.2f Z=%.2f", pitchHalf_[0], pitchHalf_[1]);
        // Real pitch GLB is in meters (~105m x 68m), scale down to fit camera view
        scene_.nodes[pitch].local.scale[0] = 0.1f;
        scene_.nodes[pitch].local.scale[1] = 0.1f;
        scene_.nodes[pitch].local.scale[2] = 0.1f;
    }

    // 2. Goals
    int goalL = scene_.addNode("goalL", root);
    if (!loadStaticGLB("goals.glb", scene_.nodes[goalL].staticMesh)) {
        LOGE("Could not load goals.glb, falling back to loadCube");
        scene_.nodes[goalL].staticMesh.loadCube(1.0f);
        scene_.nodes[goalL].local.position[0] = -10.5f;
        scene_.nodes[goalL].local.scale[0] = 0.5f;
        scene_.nodes[goalL].local.scale[1] = 1.2f;
        scene_.nodes[goalL].local.scale[2] = 3.0f;

        int goalR = scene_.addNode("goalR", root);
        scene_.nodes[goalR].staticMesh.loadCube(1.0f);
        scene_.nodes[goalR].local.position[0] = 10.5f;
        scene_.nodes[goalR].local.scale[0] = 0.5f;
        scene_.nodes[goalR].local.scale[1] = 1.2f;
        scene_.nodes[goalR].local.scale[2] = 3.0f;
    } else {
        // Apply same scale as pitch to keep goals correctly positioned
        scene_.nodes[goalL].local.scale[0] = 0.1f;
        scene_.nodes[goalL].local.scale[1] = 0.1f;
        scene_.nodes[goalL].local.scale[2] = 0.1f;
    }

    // 3. Ball
    int ball = scene_.addNode("ball", root);
    if (!loadStaticGLB("ball.glb", scene_.nodes[ball].staticMesh)) {
        LOGE("Could not load ball.glb, falling back to loadSphere");
        scene_.nodes[ball].staticMesh.loadSphere(0.25f, 12, 12);
        scene_.nodes[ball].local.position[1] = 0.25f;
    } else {
        // Ball model scaled larger (exaggerated) so it is clearly visible.
        // Ball POSITION is scaled by 0.1 separately each frame in renderScene.
        scene_.nodes[ball].local.scale[0] = 0.3f;
        scene_.nodes[ball].local.scale[1] = 0.3f;
        scene_.nodes[ball].local.scale[2] = 0.3f;
    }

    // 4. Stadium (Skipped to make loading instant on emulator/devices)
    int stadium = scene_.addNode("stadium", root);
    LOGI("Skipping stadium_test.glb load for performance");
    scene_.nodes[stadium].visible = false;

    // 5. Player Base skinned mesh
    SkinnedMesh playerMesh;
    if (loadSkinnedGLB("player_base.glb", playerMesh)) {
        setPlayerMesh(playerMesh);
    } else {
        LOGE("Could not load player_base.glb, falling back to skinned cube");
        loadFallbackSkinnedPlayer(playerMesh);
        setPlayerMesh(playerMesh);
    }

    scene_.update();
}

void ARRenderer::destroy() {
    Shader::destroy(cameraShader_);
    Shader::destroy(skinnedShader_);
    Shader::destroy(staticShader_);
    if (quadVbo_) glDeleteBuffers(1, &quadVbo_);
    if (uvVbo_) glDeleteBuffers(1, &uvVbo_);
    for (auto& node : scene_.nodes) {
        node.skinnedMesh.destroy();
        node.staticMesh.destroy();
    }
}

void ARRenderer::setPlayerMesh(const SkinnedMesh& mesh) {
    // Store mesh in scene graph for later cloning per player
    int playerNode = scene_.addNode("playerBase", scene_.findNode("root"));
    scene_.nodes[playerNode].skinnedMesh = mesh;
    scene_.nodes[playerNode].useSkinning = true;
}

void ARRenderer::drawCameraBackground(ARManager& ar) {
    if (ar.getCameraTextureId() == 0) return;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    Shader::use(cameraShader_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, ar.getCameraTextureId());
    Shader::setInt(cameraShader_, "u_Texture", 0);

    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, uvVbo_);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

void ARRenderer::renderScene(ARManager& ar, const float* playerPositions, int numPlayers,
                               const float* ballPosition,
                               const float* boneMatrices, int numBones) {
    bool tracked = ar.isMarkerTracked();
    ARPose anchorPose = ar.getMarkerAnchorPose();

    float fallbackAnchor[16];
    if (anchorPose.valid) {
        std::memcpy(fallbackAnchor, anchorPose.matrix, 16 * sizeof(float));
    } else {
        // Fixed full-pitch TV view: identity anchor (no ball tracking) so the
        // whole pitch and all 22 players are visible. The elevated broadcast
        // camera is defined in ARManager::getViewMatrix.
        mat4Identity(fallbackAnchor);
    }
    const float* anchorMat = fallbackAnchor;

    // Log positions and tracking state once in a while to avoid spamming
    static int frameCounter = 0;
    if (frameCounter++ % 120 == 0) {
        if (ballPosition && playerPositions) {
            LOGI("[ARRenderer] Ball Pos: (%f, %f, %f), Player 0 Pos: (%f, %f, %f), Tracked: %d, NumBones: %d",
                 ballPosition[0], ballPosition[1], ballPosition[2],
                 playerPositions[0], playerPositions[1], playerPositions[2],
                 tracked ? 1 : 0, numBones);
        } else {
            LOGI("[ARRenderer] Missing positions data! Ball: %p, Players: %p", ballPosition, playerPositions);
        }
    }

    float view[16], proj[16];
    ar.getViewMatrix(view);
    ar.getProjectionMatrix(proj, 0.01f, 100.0f);

    // Combine ARCore view/proj
    float vp[16];
    mat4Mul(proj, view, vp);

    // Combine with anchor matrix to map pitch space to clip space
    float vpa[16];
    mat4Mul(vp, anchorMat, vpa);

    // Update scene graph relative to pitch space root (identity)
    int rootIdx = scene_.findNode("root");
    if (rootIdx >= 0) {
        mat4Identity(scene_.nodes[rootIdx].worldMatrix);
        
        // Dynamically update ball position in scene graph.
        // GF env-coord: ball[0]=length[-1,1], ball[1]=width, ball[2]=height(m).
        int ballIdx = scene_.findNode("ball");
        if (ballIdx >= 0 && ballPosition) {
            const float halfLen = pitchHalf_[0] * 0.1f;
            const float scaleX  = halfLen;
            const float scaleZ  = halfLen * (83.6f / 54.4f);
            scene_.nodes[ballIdx].local.position[0] = ballPosition[0] * scaleX;
            scene_.nodes[ballIdx].local.position[1] = ballPosition[2] * 0.1f + 0.08f; // height
            scene_.nodes[ballIdx].local.position[2] = ballPosition[1] * scaleZ;       // width
        }
        
        scene_.update();
    }

    renderStaticObjects(vpa);
    renderPlayers(vpa, playerPositions, numPlayers, boneMatrices, numBones);
}

void ARRenderer::renderStaticObjects(const float* viewProj) {
    Shader::use(staticShader_);
    GLint mvpLoc = glGetUniformLocation(staticShader_, "u_ModelViewProj");
    GLint colLoc = glGetUniformLocation(staticShader_, "u_Color");
    GLint matLoc = glGetUniformLocation(staticShader_, "u_MaterialType");
    GLint pitchHalfLoc = glGetUniformLocation(staticShader_, "u_PitchHalf");
    glUniform2f(pitchHalfLoc, pitchHalf_[0], pitchHalf_[1]);

    for (auto& node : scene_.nodes) {
        if (node.useSkinning || !node.visible) continue;
        if (!node.staticMesh.hasData()) continue;

        float mvp[16];
        mat4Mul(viewProj, node.worldMatrix, mvp);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

        int materialType = 0;
        if (node.name == "pitch") {
            glUniform3f(colLoc, 0.15f, 0.45f, 0.15f);
            materialType = 1; // procedural grass + lines
        } else if (node.name.find("goal") == 0) {
            glUniform3f(colLoc, 0.95f, 0.95f, 0.95f);
            materialType = 2;
        } else if (node.name == "ball") {
            glUniform3f(colLoc, 1.0f, 1.0f, 1.0f);
            materialType = 3; // procedural football pattern
        } else {
            glUniform3f(colLoc, 1.0f, 1.0f, 1.0f);
            materialType = 0;
        }
        glUniform1i(matLoc, materialType);

        node.staticMesh.draw();
    }
}

void ARRenderer::renderPlayers(const float* viewProj, const float* playerPositions, int numPlayers,
                              const float* boneMatrices, int numBones) {
    int baseIdx = scene_.findNode("playerBase");
    
    // Log player rendering info every 120 frames
    static int playerFrameCounter = 0;
    bool shouldLog = (playerFrameCounter++ % 120 == 0);
    if (shouldLog) {
        LOGI("[ARRenderer::renderPlayers] baseIdx: %d, hasMesh: %d, numPlayers: %d, numBones: %d",
             baseIdx, (baseIdx >= 0) ? scene_.nodes[baseIdx].skinnedMesh.hasData() : 0, numPlayers, numBones);
    }

    if (baseIdx < 0 || !scene_.nodes[baseIdx].skinnedMesh.hasData()) return;

    if (boneMatrices && numBones > 0) {
        if (shouldLog) {
            LOGI("[ARRenderer::renderPlayers] Using SKINNED pipeline with %d bones", numBones);
        }
        Shader::use(skinnedShader_);
        GLint mvpLoc = glGetUniformLocation(skinnedShader_, "u_ModelViewProj");
        GLint colLoc = glGetUniformLocation(skinnedShader_, "u_Color");
        GLint boneLoc = glGetUniformLocation(skinnedShader_, "u_BoneMatrices");
        glUniformMatrix4fv(boneLoc, numBones, GL_FALSE, boneMatrices);

        // GF env-coord -> scene mapping (see GameplayFootball Position::env_coord)
        //  gf[0] = length  in [-1,1]   -> scene X
        //  gf[1] = width   in [-0.42,0.42] (meters = *83.6) -> scene Z (depth)
        //  gf[2] = height  (meters = *1)   -> scene Y (up)
        const float halfLen = pitchHalf_[0] * 0.1f;          // scene half-length
        const float scaleX  = halfLen;                       // gf[0]=1 -> pitch end
        const float scaleZ  = halfLen * (83.6f / 54.4f);     // width, proportional
        const float modelScale = 0.4f;
        for (int i = 0; i < numPlayers; ++i) {
            float gx = playerPositions[i * 3 + 0];
            float gw = playerPositions[i * 3 + 1];
            float gh = playerPositions[i * 3 + 2];
            float localModel[16] = {
                modelScale, 0, 0, 0,
                0, modelScale, 0, 0,
                0, 0, modelScale, 0,
                gx * scaleX,
                gh * 0.1f + 0.2f,
                gw * scaleZ,
                1
            };

            float mvp[16];
            mat4Mul(viewProj, localModel, mvp);
            glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

            bool teamA = (i < 11);
            glUniform3f(colLoc, teamA ? 0.0f : 1.0f, teamA ? 0.5f : 0.0f, teamA ? 1.0f : 0.0f);
            scene_.nodes[baseIdx].skinnedMesh.draw();
        }
    } else {
        if (shouldLog) {
            LOGI("[ARRenderer::renderPlayers] Using STATIC fallback pipeline");
        }
        // Fallback: Render players statically using staticShader_ (no bone matrices)
        Shader::use(staticShader_);
        GLint mvpLoc = glGetUniformLocation(staticShader_, "u_ModelViewProj");
        GLint colLoc = glGetUniformLocation(staticShader_, "u_Color");

        const float halfLen = pitchHalf_[0] * 0.1f;
        const float scaleX  = halfLen;
        const float scaleZ  = halfLen * (83.6f / 54.4f);
        const float modelScale = 0.4f;
        for (int i = 0; i < numPlayers; ++i) {
            float gx = playerPositions[i * 3 + 0];
            float gw = playerPositions[i * 3 + 1];
            float gh = playerPositions[i * 3 + 2];
            float localModel[16] = {
                modelScale, 0, 0, 0,
                0, modelScale, 0, 0,
                0, 0, modelScale, 0,
                gx * scaleX,
                gh * 0.1f + 0.2f,
                gw * scaleZ,
                1
            };

            float mvp[16];
            mat4Mul(viewProj, localModel, mvp);
            glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

            bool teamA = (i < 11);
            glUniform3f(colLoc, teamA ? 0.0f : 1.0f, teamA ? 0.5f : 0.0f, teamA ? 1.0f : 0.0f);
            scene_.nodes[baseIdx].skinnedMesh.draw();
        }
    }
}
 
