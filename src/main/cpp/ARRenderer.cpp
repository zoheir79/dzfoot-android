#include "ARRenderer.h"
#include "Shader.h"
#include "Mesh.h"
#include "GLBLoader.h"
#include "TouchController.h"
#include "AssetLoader.h"
#include "AnimationPlayer.h"
#include "protocol/DZFootProtocol.h"
#include <android/log.h>
#include <jni.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <algorithm>

#define LOG_TAG "ARRenderer"

static inline double nowMs() {
    auto t = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t.time_since_epoch()).count();
}
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

extern AAssetManager* gAssetManager;
extern JavaVM* gJavaVM;
extern jobject gActivityObj;

static GLuint uploadRgbaTexture(int width, int height, const uint8_t* pixels) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Enable 8x anisotropic filtering to completely destroy any distant moiré/aliasing patterns
    #ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
    #define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
    #endif
    #ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
    #define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
    #endif
    GLfloat maxAniso = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (maxAniso > 1.0f) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::fmin(maxAniso, 8.0f));
    }

    glGenerateMipmap(GL_TEXTURE_2D);
    return tex;
}

static GLuint loadAssetTexture(const char* path) {
    if (!gJavaVM || !gActivityObj) return 0;
    JNIEnv* env = nullptr;
    bool detach = false;
    jint status = gJavaVM->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (gJavaVM->AttachCurrentThread(&env, nullptr) != JNI_OK) return 0;
        detach = true;
    } else if (status != JNI_OK) {
        return 0;
    }

    GLuint tex = 0;
    jclass cls = env->FindClass("com/football/ar/JniBridge");
    jmethodID method = cls ? env->GetStaticMethodID(cls, "decodeAssetRgba", "(Landroid/content/Context;Ljava/lang/String;)[B") : nullptr;
    jstring jpath = env->NewStringUTF(path);
    jbyteArray arr = method ? static_cast<jbyteArray>(env->CallStaticObjectMethod(cls, method, gActivityObj, jpath)) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        arr = nullptr;
    }
    if (arr) {
        jsize len = env->GetArrayLength(arr);
        if (len > 8) {
            jbyte* data = env->GetByteArrayElements(arr, nullptr);
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
            int width = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
            int height = bytes[4] | (bytes[5] << 8) | (bytes[6] << 16) | (bytes[7] << 24);
            if (width > 0 && height > 0 && len >= 8 + width * height * 4) {
                tex = uploadRgbaTexture(width, height, bytes + 8);
                LOGI("Loaded texture %s (%dx%d)", path, width, height);
            }
            env->ReleaseByteArrayElements(arr, data, JNI_ABORT);
        }
        env->DeleteLocalRef(arr);
    }
    if (jpath) env->DeleteLocalRef(jpath);
    if (cls) env->DeleteLocalRef(cls);
    if (detach) gJavaVM->DetachCurrentThread();
    return tex;
}

// Simple procedural texture generator for player materials
static GLuint generateProceduralTexture(int type) {
    const int W = 64, H = 64;
    uint8_t pixels[W * H * 4];
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int i = (y * W + x) * 4;
            float nx = (float)x / W;
            float ny = (float)y / H;
            if (type == 0) { // skin
                float noise = (float)(rand() % 8) - 4.0f;
                pixels[i+0] = (uint8_t)fmax(0.0f, fmin(255.0f, 215.0f + noise));
                pixels[i+1] = (uint8_t)fmax(0.0f, fmin(255.0f, 175.0f + noise));
                pixels[i+2] = (uint8_t)fmax(0.0f, fmin(255.0f, 145.0f + noise));
                pixels[i+3] = 255;
            } else if (type == 1) { // kit
                float fabric = ((x ^ y) & 3) * 2.0f;
                uint8_t val = (uint8_t)(235.0f + fabric);
                pixels[i+0] = val;
                pixels[i+1] = val;
                pixels[i+2] = val;
                pixels[i+3] = 255;
            } else if (type == 2) { // shoe
                pixels[i+0] = 18;
                pixels[i+1] = 18;
                pixels[i+2] = 18;
                pixels[i+3] = 255;
            } else { // short
                float fabric = ((x + y) & 3) * 2.0f;
                uint8_t val = (uint8_t)(225.0f + fabric);
                pixels[i+0] = val;
                pixels[i+1] = val;
                pixels[i+2] = val;
                pixels[i+3] = 255;
            }
        }
    }
    return uploadRgbaTexture(W, H, pixels);
}

// Deterministic skin texture generator matching the 7 export avatar skin tones
static GLuint generateSkinTexture(int skinColor) {
    const int W = 64, H = 64;
    uint8_t pixels[W * H * 4];
    
    // Define base RGB values for the 7 deterministic skin tones
    uint8_t r = 205, g = 155, b = 115; // default olive (skinColor 3)
    switch (skinColor) {
        case 0: r = 245; g = 220; b = 205; break; // fair
        case 1: r = 235; g = 195; b = 165; break; // light
        case 2: r = 225; g = 180; b = 145; break; // medium
        case 3: r = 205; g = 155; b = 115; break; // olive
        case 4: r = 180; g = 130; b = 95;  break; // dark olive
        case 5: r = 145; g = 95;  b = 65;  break; // brown
        case 6: r = 80;  g = 50;  b = 35;  break; // black
        default: break;
    }

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int i = (y * W + x) * 4;
            // Add subtle procedural skin noise for realism
            float noise = (float)(rand() % 6) - 3.0f;
            pixels[i+0] = (uint8_t)fmax(0.0f, fmin(255.0f, r + noise));
            pixels[i+1] = (uint8_t)fmax(0.0f, fmin(255.0f, g + noise));
            pixels[i+2] = (uint8_t)fmax(0.0f, fmin(255.0f, b + noise));
            pixels[i+3] = 255;
        }
    }
    return uploadRgbaTexture(W, H, pixels);
}

// Deterministic hair color texture generator matching 8 export avatar hair colors
static GLuint generateHairTexture(int hairColor) {
    const int W = 64, H = 64;
    uint8_t pixels[W * H * 4];

    // Base RGB for 8 hair colors: black, dark_brown, brown, light_brown, blonde, red, grey, white
    uint8_t r = 13, g = 13, b = 13; // default black (hairColor 0)
    switch (hairColor) {
        case 0: r = 13;  g = 13;  b = 13;  break; // black
        case 1: r = 51;  g = 31;  b = 15;  break; // dark_brown
        case 2: r = 89;  g = 56;  b = 31;  break; // brown
        case 3: r = 115; g = 77;  b = 46;  break; // light_brown
        case 4: r = 217; g = 191; b = 115; break; // blonde
        case 5: r = 179; g = 64;  b = 38;  break; // red
        case 6: r = 140; g = 140; b = 140; break; // grey
        case 7: r = 230; g = 230; b = 217; break; // white
        default: break;
    }

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int i = (y * W + x) * 4;
            float noise = (float)(rand() % 10) - 5.0f;
            pixels[i+0] = (uint8_t)fmax(0.0f, fmin(255.0f, r + noise));
            pixels[i+1] = (uint8_t)fmax(0.0f, fmin(255.0f, g + noise));
            pixels[i+2] = (uint8_t)fmax(0.0f, fmin(255.0f, b + noise));
            pixels[i+3] = 255;
        }
    }
    return uploadRgbaTexture(W, H, pixels);
}

// Generate a procedural kit texture with team primary/secondary colors
static GLuint generateTeamKitTexture(uint8_t r1, uint8_t g1, uint8_t b1,
                                     uint8_t r2, uint8_t g2, uint8_t b2,
                                     uint8_t number) {
    (void)number; // TODO: draw actual number digits
    const int W = 256, H = 256;
    uint8_t pixels[W * H * 4];
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int i = (y * W + x) * 4;
            float nx = (float)x / W;
            float ny = (float)y / H;

            // Jersey UV layout: torso center, sleeves on sides, shoulders top
            bool isSleeve = (nx < 0.15f || nx > 0.85f) && ny > 0.30f && ny < 0.65f;
            bool isShoulder = ny < 0.25f;
            bool isCollar = (std::abs(nx - 0.5f) < 0.08f) && ny < 0.12f;
            bool isSideSeam = std::abs(nx - 0.15f) < 0.02f || std::abs(nx - 0.85f) < 0.02f;
            bool isBottomHem = ny > 0.92f;

            // Vertical stripes pattern
            float stripe = nx * 5.0f - std::floor(nx * 5.0f);
            bool isPrimary = stripe < 0.35f || (stripe >= 0.40f && stripe < 0.75f) || stripe >= 0.85f;

            // Sleeves and shoulders use secondary color
            bool useSecondary = isSleeve || isShoulder;
            if (useSecondary) isPrimary = false;

            uint8_t rr = isPrimary ? r1 : r2;
            uint8_t gg = isPrimary ? g1 : g2;
            uint8_t bb = isPrimary ? b1 : b2;

            // Collar white trim
            if (isCollar) { rr = 240; gg = 240; bb = 240; }

            // Darken side seams and bottom hem for depth
            float darken = 1.0f;
            if (isSideSeam || isBottomHem) darken = 0.75f;
            if (isCollar) darken = 1.0f;

            // Central number area (subtle lighter rectangle)
            bool inNumberArea = (nx >= 0.30f && nx <= 0.70f && ny >= 0.30f && ny <= 0.60f);
            if (inNumberArea) darken *= 1.15f;

            // Fabric noise
            float noise = (float)(rand() % 14) - 7.0f;
            pixels[i+0] = (uint8_t)fmax(0.0f, fmin(255.0f, (rr * darken) + noise));
            pixels[i+1] = (uint8_t)fmax(0.0f, fmin(255.0f, (gg * darken) + noise));
            pixels[i+2] = (uint8_t)fmax(0.0f, fmin(255.0f, (bb * darken) + noise));
            pixels[i+3] = 255;
        }
    }
    return uploadRgbaTexture(W, H, pixels);
}

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

bool PlayerRig::load(const char* filename) {
    if (!gAssetManager) return false;
    std::vector<uint8_t> bytes = AssetLoader::loadAsBytes(gAssetManager, filename);
    if (bytes.empty()) {
        LOGE("PlayerRig: Failed to read asset %s", filename);
        return false;
    }

    GLBLoader loader;
    GLBScene scene;
    if (!loader.load(bytes.data(), bytes.size(), scene)) {
        LOGE("PlayerRig: Failed to parse GLB %s", filename);
        return false;
    }

    nodes.resize(scene.nodes.size());
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        const auto& gn = scene.nodes[i];
        RigNode& rn = nodes[i];
        rn.name = gn.name;
        rn.parentIndex = gn.parentIndex;
        std::memcpy(rn.localMatrix, gn.localMatrix, 16 * sizeof(float));
        std::memcpy(rn.bindT, gn.bindT, 3 * sizeof(float));
        std::memcpy(rn.bindR, gn.bindR, 4 * sizeof(float));
        std::memcpy(rn.bindS, gn.bindS, 3 * sizeof(float));

        extern AnimationPlayer gAnimPlayer;
        rn.boneIndex = gAnimPlayer.findBoneIndex(rn.name);

        if (gn.meshIndex >= 0 && gn.meshIndex < (int)scene.meshes.size()) {
            const auto& mesh = scene.meshes[gn.meshIndex];

            for (const auto& prim : mesh.primitives) {
                if (!prim.vertices.empty()) {
                    SkinnedMesh m;
                    m.upload(prim.vertices, prim.indices);
                    MeshPart mp;
                    mp.mesh = std::move(m);
                    mp.materialIndex = prim.materialIndex;
                    mp.materialName = prim.materialName;
                    if (prim.materialIndex >= 0 && prim.materialIndex < (int)scene.materials.size()) {
                        std::memcpy(mp.baseColor, scene.materials[prim.materialIndex].baseColor, 4 * sizeof(float));
                    }
                    rn.staticMeshes.push_back(std::move(mp));
                }
            }
        }
    }
    // Store skin data (if any) for bone matrix computation
    if (!scene.skins.empty()) {
        skin = scene.skins[0];
        hasSkin = true;
        LOGI("PlayerRig: skin loaded with %zu joints", skin.jointIndices.size());
    }
    // Store embedded animation clips (these match what the user verified in Three.js)
    animations = std::move(scene.animations);
    LOGI("PlayerRig: %zu embedded animations", animations.size());
    for (size_t a = 0; a < animations.size(); ++a) {
        LOGI("  Anim[%zu] '%s' dur=%.2fs channels=%zu samplers=%zu", a,
             animations[a].name.c_str(), animations[a].duration,
             animations[a].channels.size(), animations[a].samplers.size());
        // Log channel targets for first clip to diagnose targetNode mapping
        if (a == 0) {
            for (size_t c = 0; c < animations[a].channels.size(); ++c) {
                const auto& ch = animations[a].channels[c];
                LOGI("    ch[%zu] targetNode=%d path=%d sampler=%d", c, ch.targetNode, ch.path, ch.sampler);
            }
        }
    }
    defaultSkinTex = loadAssetTexture("beta2/media/objects/players/textures/skin.jpg");
    skinTex = defaultSkinTex ? defaultSkinTex : generateProceduralTexture(0);
    for (int s = 0; s < 7; ++s) {
        skinTexs[s] = generateSkinTexture(s);
    }
    kitTex = loadAssetTexture("beta2/media/objects/players/textures/kit_template.png");
    if (!kitTex) kitTex = generateProceduralTexture(1);
    shoeTex = loadAssetTexture("beta2/media/objects/players/textures/shoe.jpg");
    if (!shoeTex) shoeTex = generateProceduralTexture(2);
    shortTex = kitTex ? kitTex : generateProceduralTexture(3);

    LOGI("PlayerRig: loaded '%s' with %zu nodes", filename, nodes.size());
    // Only log the first 30 rig/mesh nodes; skip the 1100+ empty material/texture nodes
    for (size_t i = 0; i < std::min(nodes.size(), size_t(30)); ++i) {
        const auto& rn = nodes[i];
        if (!rn.name.empty() || rn.staticMeshes.size() > 0) {
            LOGI("  Node[%zu] name='%s' parent=%d bone=%d meshes=%zu bindS=(%.3f,%.3f,%.3f)",
                 i, rn.name.c_str(), rn.parentIndex, rn.boneIndex, rn.staticMeshes.size(),
                 rn.bindS[0], rn.bindS[1], rn.bindS[2]);
        }
    }
    return true;
}

void PlayerRig::destroy() {
    for (auto& rn : nodes) {
        for (auto& part : rn.staticMeshes) {
            part.mesh.destroy();
        }
        rn.staticMeshes.clear();
    }
    nodes.clear();
    skin = GLBSkin();
    hasSkin = false;
    for (int s = 0; s < 7; ++s) {
        if (skinTexs[s]) {
            glDeleteTextures(1, &skinTexs[s]);
            skinTexs[s] = 0;
        }
    }
    if (skinTex)  { glDeleteTextures(1, &skinTex);  skinTex = 0; }
    if (kitTex)   { glDeleteTextures(1, &kitTex);   kitTex = 0; }
    if (shoeTex)  { glDeleteTextures(1, &shoeTex);  shoeTex = 0; }
    if (shortTex && shortTex != kitTex) { glDeleteTextures(1, &shortTex); }
    shortTex = 0;
    defaultSkinTex = 0;
    scratchT.clear(); scratchT.shrink_to_fit();
    scratchR.clear(); scratchR.shrink_to_fit();
    scratchS.clear(); scratchS.shrink_to_fit();
    scratchCurT.clear(); scratchCurT.shrink_to_fit();
    scratchCurR.clear(); scratchCurR.shrink_to_fit();
    scratchCurS.clear(); scratchCurS.shrink_to_fit();
    scratchPrevT.clear(); scratchPrevT.shrink_to_fit();
    scratchPrevR.clear(); scratchPrevR.shrink_to_fit();
    scratchPrevS.clear(); scratchPrevS.shrink_to_fit();
    scratchGlobalMats.clear(); scratchGlobalMats.shrink_to_fit();
    // Destroy attached modular meshes
    for (auto& part : attachedMeshes) {
        part.mesh.destroy();
    }
    attachedMeshes.clear();
    for (int s = 0; s < 8; ++s) {
        if (hairTexs[s]) { glDeleteTextures(1, &hairTexs[s]); hairTexs[s] = 0; }
    }
}

// ─── Modular Avatar Composition ───────────────────────────────────

int PlayerRig::findNodeIndex(const char* name) const {
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

bool PlayerRig::loadBody(const char* bodyGlb) {
    // Reuse existing load() logic but clear previous state first
    destroy();
    if (!gAssetManager) return false;
    std::vector<uint8_t> bytes = AssetLoader::loadAsBytes(gAssetManager, bodyGlb);
    if (bytes.empty()) {
        LOGE("PlayerRig: Failed to read body asset %s", bodyGlb);
        return false;
    }
    GLBLoader loader;
    GLBScene scene;
    if (!loader.load(bytes.data(), bytes.size(), scene)) {
        LOGE("PlayerRig: Failed to parse body GLB %s", bodyGlb);
        return false;
    }

    nodes.resize(scene.nodes.size());
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        const auto& gn = scene.nodes[i];
        RigNode& rn = nodes[i];
        rn.name = gn.name;
        rn.parentIndex = gn.parentIndex;
        std::memcpy(rn.localMatrix, gn.localMatrix, 16 * sizeof(float));
        std::memcpy(rn.bindT, gn.bindT, 3 * sizeof(float));
        std::memcpy(rn.bindR, gn.bindR, 4 * sizeof(float));
        std::memcpy(rn.bindS, gn.bindS, 3 * sizeof(float));

        extern AnimationPlayer gAnimPlayer;
        rn.boneIndex = gAnimPlayer.findBoneIndex(rn.name);

        if (gn.meshIndex >= 0 && gn.meshIndex < (int)scene.meshes.size()) {
            const auto& mesh = scene.meshes[gn.meshIndex];
            for (const auto& prim : mesh.primitives) {
                if (!prim.vertices.empty()) {
                    SkinnedMesh m;
                    m.upload(prim.vertices, prim.indices);
                    MeshPart mp;
                    mp.mesh = std::move(m);
                    mp.materialIndex = prim.materialIndex;
                    mp.materialName = prim.materialName;
                    if (prim.materialIndex >= 0 && prim.materialIndex < (int)scene.materials.size()) {
                        std::memcpy(mp.baseColor, scene.materials[prim.materialIndex].baseColor, 4 * sizeof(float));
                    }
                    rn.staticMeshes.push_back(std::move(mp));
                }
            }
        }
    }
    if (!scene.skins.empty()) {
        skin = scene.skins[0];
        hasSkin = true;
    }
    animations = std::move(scene.animations);

    // Load shared textures (kit, shoe) — skin/hair swapped per-player at runtime
    skinTex = generateProceduralTexture(0); // fallback skin tone (warm olive)
    kitTex = loadAssetTexture("modular/textures/kit_template.png");
    if (!kitTex) kitTex = generateProceduralTexture(1);
    shoeTex = loadAssetTexture("modular/textures/shoe.png");
    if (!shoeTex) shoeTex = generateProceduralTexture(2);
    shortTex = kitTex ? kitTex : generateProceduralTexture(3);

    // Pre-generate 7 skin tone textures and 8 hair color textures
    for (int s = 0; s < 7; ++s) {
        skinTexs[s] = generateSkinTexture(s);
    }
    for (int h = 0; h < 8; ++h) {
        hairTexs[h] = generateHairTexture(h);
    }

    LOGI("PlayerRig: modular body '%s' loaded with %zu nodes, %zu anims",
         bodyGlb, nodes.size(), animations.size());
    return true;
}

bool PlayerRig::attachPart(const char* partGlb, const char* parentBoneName,
                           const char* materialCat) {
    if (!gAssetManager) return false;
    std::vector<uint8_t> bytes = AssetLoader::loadAsBytes(gAssetManager, partGlb);
    if (bytes.empty()) {
        LOGI("PlayerRig: optional part %s not found, skipping", partGlb);
        return true; // not fatal — part may be intentionally missing (bald, none)
    }
    GLBLoader loader;
    GLBScene scene;
    if (!loader.load(bytes.data(), bytes.size(), scene)) {
        LOGE("PlayerRig: Failed to parse part GLB %s", partGlb);
        return false;
    }
    if (scene.meshes.empty()) {
        LOGI("PlayerRig: part %s has no mesh (empty), skipping", partGlb);
        return true;
    }

    int parentIdx = findNodeIndex(parentBoneName);
    if (parentIdx < 0) {
        LOGE("PlayerRig: parent bone '%s' not found for part %s", parentBoneName, partGlb);
        return false;
    }

    // Transfer all primitives from the part's meshes into attachedMeshes
    for (const auto& mesh : scene.meshes) {
        for (auto prim : mesh.primitives) {
            if (prim.vertices.empty()) continue;
            // Scale up hair/beard for visibility and shift up so they sit above the head mesh
            float scale = 1.0f;
            float shiftY = 0.0f;
            if (strcmp(materialCat, "hair") == 0) {
                scale = 1.05f;   // slightly scale up so it fits neck-based head alignment perfectly
                shiftY = 0.0f;   // aligned with bone
            } else if (strcmp(materialCat, "beard") == 0) {
                scale = 1.05f;   // slightly scale up
                shiftY = 0.0f;
            }
            if (scale != 1.0f || shiftY != 0.0f) {
                for (auto& v : prim.vertices) {
                    v.pos[0] *= scale;
                    v.pos[1] = v.pos[1] * scale + shiftY;
                    v.pos[2] *= scale;
                }
            }
            SkinnedMesh m;
            m.upload(prim.vertices, prim.indices);
            MeshPart mp;
            mp.mesh = std::move(m);
            mp.materialIndex = prim.materialIndex;
            mp.materialName = materialCat; // override with client's category
            mp.parentNodeIndex = parentIdx; // remember which bone this part is parented to
            // Copy base color if material exists
            if (prim.materialIndex >= 0 && prim.materialIndex < (int)scene.materials.size()) {
                std::memcpy(mp.baseColor, scene.materials[prim.materialIndex].baseColor, 4 * sizeof(float));
            }
            attachedMeshes.push_back(std::move(mp));
            LOGI("PlayerRig: attached part %s to '%s' (verts=%zu scale=%.1f shift=%.2f)",
                 partGlb, parentBoneName, prim.vertices.size(), scale, shiftY);
        }
    }
    return true;
}

bool PlayerRig::loadModular(const AvatarConfig& cfg) {
    // 1. Load body template (skeleton + mesh + animations)
    const char* body_names[4] = {"thin", "average", "muscular", "heavy"};
    const char* bt = body_names[cfg.bodyType % 4];
    char bodyPath[128];
    snprintf(bodyPath, sizeof(bodyPath), "modular/bodies/body_%s.glb", bt);
    if (!loadBody(bodyPath)) return false;

    // 2. Apply height scale on root node proportionally relative to standard 1.80m height
    int rootIdx = findNodeIndex("player");
    if (rootIdx >= 0) {
        float scaleFactor = cfg.height / 1.80f;
        nodes[rootIdx].bindS[0] = scaleFactor;
        nodes[rootIdx].bindS[1] = scaleFactor;
        nodes[rootIdx].bindS[2] = scaleFactor;
    }

    // 3. Attach hair (if not bald)
    if (cfg.hairStyle != 5) { // 5 = bald
        const char* hair_names[6] = {"short", "long", "mohawk", "curly", "ponytail", "bald"};
        const char* hs = hair_names[cfg.hairStyle % 6];
        char hairPath[128];
        snprintf(hairPath, sizeof(hairPath), "modular/parts/hair_%s.glb", hs);
        attachPart(hairPath, "neck", "hair");
    }

    // 4. Attach beard (if not none)
    if (cfg.beardStyle != 0) { // 0 = none
        const char* beard_names[4] = {"none", "stubble", "short", "full"};
        const char* bs = beard_names[cfg.beardStyle % 4];
        char beardPath[128];
        snprintf(beardPath, sizeof(beardPath), "modular/parts/beard_%s.glb", bs);
        attachPart(beardPath, "neck", "beard");
    }

    static const char* hairLabels[6] = {"short","long","mohawk","curly","ponytail","bald"};
    const char* hairLabel = (cfg.hairStyle < 6) ? hairLabels[cfg.hairStyle] : "?";
    LOGI("PlayerRig: modular loaded (body=%s hair=%d(%s) beard=%d skin=%d height=%.2f num=%d)",
         bt, cfg.hairStyle, hairLabel, cfg.beardStyle, cfg.skinColor, cfg.height, cfg.playerNumber);
    return true;
}

// Compose a column-major TRS matrix (matches GLBLoader convention)
static void composeTRS(const float* t, const float* r, const float* s, float* m) {
    float qx = r[0], qy = r[1], qz = r[2], qw = r[3];
    float xx = qx*qx, yy = qy*qy, zz = qz*qz;
    float xy = qx*qy, xz = qx*qz, yz = qy*qz;
    float wx = qw*qx, wy = qw*qy, wz = qw*qz;
    m[0]  = (1 - 2*(yy+zz)) * s[0]; m[1]  = (2*(xy+wz)) * s[0]; m[2]  = (2*(xz-wy)) * s[0]; m[3]  = 0;
    m[4]  = (2*(xy-wz)) * s[1]; m[5]  = (1 - 2*(xx+zz)) * s[1]; m[6]  = (2*(yz+wx)) * s[1]; m[7]  = 0;
    m[8]  = (2*(xz+wy)) * s[2]; m[9]  = (2*(yz-wx)) * s[2]; m[10] = (1 - 2*(xx+yy)) * s[2]; m[11] = 0;
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1;
}

static void quatSlerpLocal(const float* a, const float* b, float t, float* out) {
    float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
    float b0[4] = { b[0], b[1], b[2], b[3] };
    if (dot < 0.0f) { dot = -dot; b0[0]=-b0[0]; b0[1]=-b0[1]; b0[2]=-b0[2]; b0[3]=-b0[3]; }
    if (dot > 0.9995f) {
        for (int i = 0; i < 4; ++i) out[i] = a[i] + t*(b0[i]-a[i]);
    } else {
        float theta0 = std::acos(dot);
        float theta = theta0 * t;
        float st = std::sin(theta), st0 = std::sin(theta0);
        float s0 = std::cos(theta) - dot * st / st0;
        float s1 = st / st0;
        for (int i = 0; i < 4; ++i) out[i] = a[i]*s0 + b0[i]*s1;
    }
    float len = std::sqrt(out[0]*out[0]+out[1]*out[1]+out[2]*out[2]+out[3]*out[3]);
    if (len > 1e-6f) { out[0]/=len; out[1]/=len; out[2]/=len; out[3]/=len; }
}

static void quatMul(const float* q1, const float* q2, float* out) {
    float x1 = q1[0], y1 = q1[1], z1 = q1[2], w1 = q1[3];
    float x2 = q2[0], y2 = q2[1], z2 = q2[2], w2 = q2[3];

    out[0] = w1*x2 + x1*w2 + y1*z2 - z1*y2;
    out[1] = w1*y2 - x1*z2 + y1*w2 + z1*x2;
    out[2] = w1*z2 + x1*y2 - y1*x2 + z1*w2;
    out[3] = w1*w2 - x1*x2 - y1*y2 - z1*z2;
}

// Sample a glTF animation sampler at time t into out (comps floats)
static void sampleSampler(const GLBAnimSampler& s, float t, float* out) {
    int comps = s.components > 0 ? s.components : 4;
    int stride = comps, valOff = 0;
    if (s.interpolation == 2) { stride = comps * 3; valOff = comps; } // CUBICSPLINE: take value term
    int n = (int)s.input.size();
    if (n == 0 || (int)s.output.size() < stride) {
        for (int i = 0; i < comps; ++i) out[i] = (comps == 4 && i == 3) ? 1.0f : 0.0f;
        return;
    }
    if (t <= s.input.front()) {
        for (int i = 0; i < comps; ++i) out[i] = s.output[valOff + i];
        return;
    }
    if (t >= s.input.back()) {
        int base = (n - 1) * stride + valOff;
        for (int i = 0; i < comps; ++i) out[i] = s.output[base + i];
        return;
    }
    int k = 0;
    while (k + 1 < n && s.input[k + 1] < t) ++k;
    float t0 = s.input[k], t1 = s.input[k + 1];
    float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
    if (s.interpolation == 1) alpha = 0.0f; // STEP
    const float* v0 = &s.output[k * stride + valOff];
    const float* v1 = &s.output[(k + 1) * stride + valOff];
    if (comps == 4) {
        quatSlerpLocal(v0, v1, alpha, out);
    } else {
        for (int i = 0; i < comps; ++i) out[i] = v0[i] + (v1[i] - v0[i]) * alpha;
    }
}

static bool isLoopingAnim(uint8_t animId);

void PlayerRig::draw(const float* viewProj, const float* playerWorld, float rotY,
                     uint8_t animId, uint8_t previousAnim, float blend, float time, float prevTime,
                     GLuint staticShader, GLuint skinnedShader, const float* teamColor,
                     GLuint kitTexture, GLuint shortTexture,
                     int playerIndex, const DirAnimClip* dirClip,
                     const AvatarConfig* avatar,
                     const float* lightSpaceMatrix, GLuint shadowTex,
                     float pitchScale) {
    if (nodes.empty()) return;

    (void)skinnedShader;

    // 1. Per-node animated TRS: start from bind pose, then override animated channels
    const size_t N = nodes.size();
    // Resize persistent scratch buffers once (no-op after first call)
    scratchT.resize(N * 3); scratchR.resize(N * 4); scratchS.resize(N * 3);
    scratchCurT.resize(N * 3); scratchCurR.resize(N * 4); scratchCurS.resize(N * 3);
    scratchPrevT.resize(N * 3); scratchPrevR.resize(N * 4); scratchPrevS.resize(N * 3);
    scratchGlobalMats.resize(N * 16);

    for (size_t i = 0; i < N; ++i) {
        std::memcpy(&scratchT[i * 3], nodes[i].bindT, 3 * sizeof(float));
        std::memcpy(&scratchR[i * 4], nodes[i].bindR, 4 * sizeof(float));
        std::memcpy(&scratchS[i * 3], nodes[i].bindS, 3 * sizeof(float));
    }

    // Logging disabled: animDbg and draw logs removed to prevent logd IPC bottleneck.
    bool animDbg = false;

    std::vector<float> &curT = scratchCurT, &curR = scratchCurR, &curS = scratchCurS;
    std::vector<float> &prevT = scratchPrevT, &prevR = scratchPrevR, &prevS = scratchPrevS;
    std::memcpy(curT.data(), scratchT.data(), N * 3 * sizeof(float));
    std::memcpy(curR.data(), scratchR.data(), N * 4 * sizeof(float));
    std::memcpy(curS.data(), scratchS.data(), N * 3 * sizeof(float));
    std::memcpy(prevT.data(), scratchT.data(), N * 3 * sizeof(float));
    std::memcpy(prevR.data(), scratchR.data(), N * 4 * sizeof(float));
    std::memcpy(prevS.data(), scratchS.data(), N * 3 * sizeof(float));

    // Sample current animation (prefer external DirAnimClip from compile bank)
    if (dirClip) {
        float phaseOffset = 0.0f;
        if (isLoopingAnim(animId) && playerIndex >= 0) {
            phaseOffset = static_cast<float>((playerIndex * 53) % 100) * 0.01f * dirClip->duration;
        }
        float sampleTime = time + phaseOffset;
        float normalizedTime = std::fmod(std::fmod(sampleTime, dirClip->duration) + dirClip->duration, dirClip->duration) / dirClip->duration;
        int frame = (int)(normalizedTime * dirClip->frameCount);
        if (frame < 0) frame = 0;
        if (frame >= dirClip->frameCount) frame = dirClip->frameCount - 1;

        const BoneFrame* bf = &dirClip->frames[frame * 14];
        for (size_t i = 0; i < 14 && i < N; ++i) {
            // Skip player (0) and body (1) animations to prevent double-rotation
            // and translation displacement (which throws players in the stands).
            // This keeps the player torso perfectly vertical, stable, and facing rotY,
            // while animating limbs (legs/arms) dynamically.
            if (i == 0 || i == 1) {
                continue;
            }

            // Apply anim rotation relative to default bind orientation (upright)
            quatMul(nodes[i].bindR, bf[i].rotation, &curR[i * 4]);
        }
    } else if (!animations.empty()) {
        size_t clipIdx = (animId < animations.size()) ? animId : 0;
        const GLBAnimation& clip = animations[clipIdx];
        float dur = clip.duration > 0.0001f ? clip.duration : 1.0f;
        float phaseOffset = 0.0f;
        if (isLoopingAnim(animId) && playerIndex >= 0) {
            // large prime spread so offsets are visibly different even for few players
            phaseOffset = static_cast<float>((playerIndex * 53) % 100) * 0.01f * dur;
        }
        float sampleTime = time + phaseOffset;
        float tt;
        if (isLoopingAnim(animId)) {
            tt = std::fmod(std::fmod(sampleTime, dur) + dur, dur); // wrap safely
        } else {
            tt = sampleTime > dur ? dur : sampleTime;
        }

        // animDbg logging removed for performance

        for (const auto& ch : clip.channels) {
            if (ch.targetNode < 0 || ch.targetNode >= (int)N) continue;
            if (ch.sampler < 0 || ch.sampler >= (int)clip.samplers.size()) continue;
            const GLBAnimSampler& s = clip.samplers[ch.sampler];
            float val[4] = { 0, 0, 0, 1 };
            sampleSampler(s, tt, val);
            if (ch.path == 0) {        // translation
                if (ch.targetNode == 0) continue;
                std::memcpy(&scratchCurT[ch.targetNode * 3], val, 3 * sizeof(float));
            } else if (ch.path == 1) { // rotation
                if (ch.targetNode == 0 || ch.targetNode == 1) continue; // Skip root and body rotations to keep upright direction
                std::memcpy(&scratchCurR[ch.targetNode * 4], val, 4 * sizeof(float));
                // animDbg logging removed for performance
            } else if (ch.path == 2) { // scale
                std::memcpy(&scratchCurS[ch.targetNode * 3], val, 3 * sizeof(float));
            }
        }
    } else if (animDbg) {
        LOGI("[animDbg] animations EMPTY (parse failed or not loaded)");
    }

    // Sample previous animation if we are blending (crossfade)
    bool isBlending = (blend < 0.999f) && (!animations.empty());
    if (isBlending) {
        size_t prevClipIdx = (previousAnim < animations.size()) ? previousAnim : 0;
        const GLBAnimation& prevClip = animations[prevClipIdx];
        float prevDur = prevClip.duration > 0.0001f ? prevClip.duration : 1.0f;
        float prevSampleTime = prevTime;
        if (isLoopingAnim(previousAnim) && playerIndex >= 0) {
            prevSampleTime += static_cast<float>((playerIndex * 53) % 100) * 0.01f * prevDur;
        }
        float prevTt;
        if (isLoopingAnim(previousAnim)) {
            prevTt = std::fmod(std::fmod(prevSampleTime, prevDur) + prevDur, prevDur);
        } else {
            prevTt = prevSampleTime > prevDur ? prevDur : prevSampleTime;
        }

        for (const auto& ch : prevClip.channels) {
            if (ch.targetNode < 0 || ch.targetNode >= (int)N) continue;
            if (ch.sampler < 0 || ch.sampler >= (int)prevClip.samplers.size()) continue;
            const GLBAnimSampler& s = prevClip.samplers[ch.sampler];
            float val[4] = { 0, 0, 0, 1 };
            sampleSampler(s, prevTt, val);
            if (ch.path == 0) {
                if (ch.targetNode == 0) continue;
                std::memcpy(&scratchPrevT[ch.targetNode * 3], val, 3 * sizeof(float));
            } else if (ch.path == 1) {
                if (ch.targetNode == 0 || ch.targetNode == 1) continue; // Skip root and body rotations to keep upright direction
                std::memcpy(&scratchPrevR[ch.targetNode * 4], val, 4 * sizeof(float));
            } else if (ch.path == 2) {
                std::memcpy(&scratchPrevS[ch.targetNode * 3], val, 3 * sizeof(float));
            }
        }
    }

    // Blend current and previous TRS poses
    for (size_t i = 0; i < N; ++i) {
        if (isBlending) {
            scratchT[i * 3 + 0] = scratchPrevT[i * 3 + 0] + blend * (scratchCurT[i * 3 + 0] - scratchPrevT[i * 3 + 0]);
            scratchT[i * 3 + 1] = scratchPrevT[i * 3 + 1] + blend * (scratchCurT[i * 3 + 1] - scratchPrevT[i * 3 + 1]);
            scratchT[i * 3 + 2] = scratchPrevT[i * 3 + 2] + blend * (scratchCurT[i * 3 + 2] - scratchPrevT[i * 3 + 2]);

            quatSlerpLocal(&scratchPrevR[i * 4], &scratchCurR[i * 4], blend, &scratchR[i * 4]);

            scratchS[i * 3 + 0] = scratchPrevS[i * 3 + 0] + blend * (scratchCurS[i * 3 + 0] - scratchPrevS[i * 3 + 0]);
            scratchS[i * 3 + 1] = scratchPrevS[i * 3 + 1] + blend * (scratchCurS[i * 3 + 1] - scratchPrevS[i * 3 + 1]);
            scratchS[i * 3 + 2] = scratchPrevS[i * 3 + 2] + blend * (scratchCurS[i * 3 + 2] - scratchPrevS[i * 3 + 2]);
        } else {
            std::memcpy(&scratchT[i * 3], &scratchCurT[i * 3], 3 * sizeof(float));
            std::memcpy(&scratchR[i * 4], &scratchCurR[i * 4], 4 * sizeof(float));
            std::memcpy(&scratchS[i * 3], &scratchCurS[i * 3], 3 * sizeof(float));
        }
    }

    // 2. Compose local matrices from animated TRS and accumulate into global matrices
    static bool parentOrderWarned = false;
    for (size_t i = 0; i < N; ++i) {
        const auto& rn = nodes[i];
        if (!parentOrderWarned && rn.parentIndex >= 0 && rn.parentIndex >= (int32_t)i) {
            parentOrderWarned = true;
            LOGE("[PlayerRig] parentIndex(%d) >= nodeIndex(%zu) for '%s' — hierarchy will be wrong!",
                 rn.parentIndex, i, rn.name.c_str());
        }
        float localMat[16];
        composeTRS(&scratchT[i * 3], &scratchR[i * 4], &scratchS[i * 3], localMat);

        float* gm = &scratchGlobalMats[i * 16];
        if (rn.parentIndex >= 0) {
            float* parentGm = &scratchGlobalMats[rn.parentIndex * 16];
            Transform::mat4Mul(parentGm, localMat, gm);
        } else {
            std::memcpy(gm, localMat, 16 * sizeof(float));
        }
    }
    // animDbg logging removed for performance

    // 3. Build player world model matrix (Translation * rotY rotation * scale)
    float modelRot[16];
    // player_base.glb faces +X (to the right) at rest, so we subtract PI/2 (1.57079632f)
    // to align the body model perfectly with the velocity/heading direction.
    const float modelYaw = rotY - 1.57079632f;
    float qRot[4] = { 0.0f, std::sin(modelYaw * 0.5f), 0.0f, std::cos(modelYaw * 0.5f) };
    Transform::quatToMat4(qRot, modelRot);

    const float scaleVal = 0.14f * pitchScale; // larger players for broadcast visibility (~2.3m proportional on virtual pitch)
    modelRot[0] *= scaleVal; modelRot[1] *= scaleVal; modelRot[2] *= scaleVal;
    modelRot[4] *= scaleVal; modelRot[5] *= scaleVal; modelRot[6] *= scaleVal;
    modelRot[8] *= scaleVal; modelRot[9] *= scaleVal; modelRot[10] *= scaleVal;
    modelRot[12] = playerWorld[0];
    modelRot[13] = playerWorld[1];
    modelRot[14] = playerWorld[2];
    modelRot[15] = 1.0f;

    // 4. player_base.glb is a segmented rigid model (no per-vertex weights).
    // Draw each mesh part with its own animated global transform via the static shader.
    GLuint shader = staticShader;
    glUseProgram(shader);

    // Cache uniform locations statically to avoid expensive glGetUniformLocation calls in the render loop
    static GLint mvpLoc = -1;
    static GLint modelLoc = -1;
    static GLint colLoc = -1;
    static GLint texLoc = -1;
    static GLint useTexLoc = -1;
    static GLuint lastShader = 0;

    static GLint lightSpaceLoc = -1;
    static GLint shadowMapLoc = -1;
    static GLint fogDensityLoc = -1;
    static GLint matLoc = -1;
    static GLint timeLoc = -1;
    static GLint blobShadowLoc = -1;
    if (shader != lastShader) {
        mvpLoc = glGetUniformLocation(shader, "u_ModelViewProj");
        modelLoc = glGetUniformLocation(shader, "u_ModelMatrix");
        colLoc = glGetUniformLocation(shader, "u_Color");
        texLoc = glGetUniformLocation(shader, "u_BaseTexture");
        useTexLoc = glGetUniformLocation(shader, "u_UseTexture");
        lightSpaceLoc = glGetUniformLocation(shader, "u_LightSpaceMatrix");
        shadowMapLoc = glGetUniformLocation(shader, "u_ShadowMap");
        fogDensityLoc = glGetUniformLocation(shader, "u_FogDensity");
        matLoc = glGetUniformLocation(shader, "u_MaterialType");
        timeLoc = glGetUniformLocation(shader, "u_Time");
        blobShadowLoc = glGetUniformLocation(shader, "u_BlobShadow");
        lastShader = shader;
    }

    // Send shadow / fog / time uniforms once per player draw
    if (blobShadowLoc >= 0) {
        glUniform1f(blobShadowLoc, 0.0f);
    }
    if (lightSpaceLoc >= 0 && lightSpaceMatrix) {
        glUniformMatrix4fv(lightSpaceLoc, 1, GL_FALSE, lightSpaceMatrix);
    }
    if (fogDensityLoc >= 0) {
        glUniform1f(fogDensityLoc, 0.025f);
    }
    if (shadowMapLoc >= 0 && shadowTex) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadowTex);
        glUniform1i(shadowMapLoc, 1);
    }
    if (matLoc >= 0) {
        glUniform1i(matLoc, 0); // force default material (specular + rim light for players)
    }
    if (timeLoc >= 0) {
        glUniform1f(timeLoc, static_cast<float>(nowMs() * 0.001));
    }

    // 5. Draw each limb with its own transform (segmented rigid-body animation)
    int drawCount = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& rn = nodes[i];
        if (rn.staticMeshes.empty()) continue;

        float partWorld[16];
        Transform::mat4Mul(modelRot, &scratchGlobalMats[i * 16], partWorld);

        // draw logging removed for performance

        float mvp[16];
        Transform::mat4Mul(viewProj, partWorld, mvp);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);
        if (modelLoc >= 0) glUniformMatrix4fv(modelLoc, 1, GL_FALSE, partWorld);

        for (const auto& part : rn.staticMeshes) {
            float partColor[3];
            partColor[0] = part.baseColor[0];
            partColor[1] = part.baseColor[1];
            partColor[2] = part.baseColor[2];

            // Helper: case-insensitive substring search for material matching
            auto matContains = [&](const char* keyword) -> bool {
                const char* s = part.materialName.c_str();
                size_t klen = strlen(keyword);
                size_t slen = part.materialName.size();
                for (size_t i = 0; i + klen <= slen; ++i) {
                    if (strncasecmp(s + i, keyword, klen) == 0) return true;
                }
                return false;
            };

            GLuint boundTex = 0;
            bool forceColor = false; // if true, override partColor with solid fallback
            int partMaterialType = 0; // 0=default, 1=skin/face/head, 2=hair/cheveux, 3=beard
            if (matContains("skin") || matContains("face") || matContains("head")) {
                partMaterialType = 5; // skin in STATIC_FRAG
                if (avatar && avatar->skinColor < 7 && skinTexs[avatar->skinColor]) {
                    boundTex = skinTexs[avatar->skinColor];
                } else {
                    boundTex = skinTex;
                }
                if (!boundTex) {
                    // Solid skin fallback (olive tone)
                    partColor[0] = 0.80f; partColor[1] = 0.60f; partColor[2] = 0.45f;
                    forceColor = true;
                }
            } else if (matContains("hair") || matContains("cheveux")) {
                partMaterialType = 6; // hair in STATIC_FRAG
                if (avatar && avatar->hairColor < 8 && hairTexs[avatar->hairColor]) {
                    boundTex = hairTexs[avatar->hairColor];
                } else if (hairTexs[0]) {
                    boundTex = hairTexs[0]; // black fallback
                }
                if (!boundTex) {
                    partColor[0] = 0.05f; partColor[1] = 0.05f; partColor[2] = 0.05f;
                    forceColor = true;
                }
            } else if (matContains("beard")) {
                partMaterialType = 7; // beard in STATIC_FRAG
                if (avatar && avatar->hairColor < 8 && hairTexs[avatar->hairColor]) {
                    boundTex = hairTexs[avatar->hairColor];
                } else if (hairTexs[0]) {
                    boundTex = hairTexs[0];
                }
                if (!boundTex) {
                    partColor[0] = 0.05f; partColor[1] = 0.05f; partColor[2] = 0.05f;
                    forceColor = true;
                }
            } else if (matContains("kit") || matContains("jersey") || matContains("upper") || matContains("torso")) {
                boundTex = kitTexture ? kitTexture : kitTex;
                if (!kitTexture) {
                    partColor[0] = teamColor[0];
                    partColor[1] = teamColor[1];
                    partColor[2] = teamColor[2];
                    forceColor = true;
                }
            } else if (matContains("short") || matContains("lower") || matContains("pant")) {
                boundTex = shortTexture ? shortTexture : shortTex;
                if (!shortTexture) {
                    partColor[0] = teamColor[0];
                    partColor[1] = teamColor[1];
                    partColor[2] = teamColor[2];
                    forceColor = true;
                }
            } else if (matContains("shoe") || matContains("foot") || matContains("chaussure")) {
                boundTex = shoeTex;
                if (!boundTex) {
                    partColor[0] = 0.07f; partColor[1] = 0.07f; partColor[2] = 0.07f;
                    forceColor = true;
                }
            } else {
                // Unknown material: if baseColor is near-white, force a grey fallback
                // so invisible geometry doesn't disappear against the sky
                float lum = (partColor[0] + partColor[1] + partColor[2]) / 3.0f;
                if (lum > 0.90f) {
                    partColor[0] = 0.55f; partColor[1] = 0.55f; partColor[2] = 0.55f;
                    forceColor = true;
                }
            }

            if (boundTex && texLoc >= 0 && useTexLoc >= 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, boundTex);
                glUniform1i(texLoc, 0);
                glUniform1f(useTexLoc, 1.0f);
            } else if (useTexLoc >= 0) {
                glUniform1f(useTexLoc, 0.0f);
            }

            if (matLoc >= 0) {
                glUniform1i(matLoc, partMaterialType);
            }

            glUniform3fv(colLoc, 1, partColor);
            part.mesh.draw();
            drawCount++;
        }
    }

    // 6. Draw attached modular meshes (hair/beard) using their parent bone transform
    if (!attachedMeshes.empty()) {
        int lastParentIdx = -2; // sentinel to avoid redundant uniform updates
        for (const auto& part : attachedMeshes) {
            int parentIdx = part.parentNodeIndex;
            if (parentIdx < 0) continue;
            if (parentIdx != lastParentIdx) {
                float parentWorld[16];
                Transform::mat4Mul(modelRot, &scratchGlobalMats[parentIdx * 16], parentWorld);
                float parentMvp[16];
                Transform::mat4Mul(viewProj, parentWorld, parentMvp);
                glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, parentMvp);
                if (modelLoc >= 0) glUniformMatrix4fv(modelLoc, 1, GL_FALSE, parentWorld);
                lastParentIdx = parentIdx;
            }
            float partColor[3] = {part.baseColor[0], part.baseColor[1], part.baseColor[2]};
            GLuint boundTex = 0;
            // Reuse same substring matching as body meshes
            auto attContains = [&](const char* keyword) -> bool {
                const char* s = part.materialName.c_str();
                size_t klen = strlen(keyword);
                size_t slen = part.materialName.size();
                for (size_t i = 0; i + klen <= slen; ++i) {
                    if (strncasecmp(s + i, keyword, klen) == 0) return true;
                }
                return false;
            };
            int partMaterialType = 0; // 0=default, 2=hair, 3=beard
            if (attContains("hair") || attContains("cheveux")) {
                partMaterialType = 6; // hair in STATIC_FRAG
                if (avatar && avatar->hairColor < 8 && hairTexs[avatar->hairColor]) {
                    boundTex = hairTexs[avatar->hairColor];
                } else if (hairTexs[0]) {
                    boundTex = hairTexs[0];
                }
                if (!boundTex) {
                    partColor[0] = 0.05f; partColor[1] = 0.05f; partColor[2] = 0.05f;
                }
            } else if (attContains("beard")) {
                partMaterialType = 7; // beard in STATIC_FRAG
                if (avatar && avatar->hairColor < 8 && hairTexs[avatar->hairColor]) {
                    boundTex = hairTexs[avatar->hairColor];
                } else if (hairTexs[0]) {
                    boundTex = hairTexs[0];
                }
                if (!boundTex) {
                    partColor[0] = 0.05f; partColor[1] = 0.05f; partColor[2] = 0.05f;
                }
            }
            if (boundTex && texLoc >= 0 && useTexLoc >= 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, boundTex);
                glUniform1i(texLoc, 0);
                glUniform1f(useTexLoc, 1.0f);
            } else if (useTexLoc >= 0) {
                glUniform1f(useTexLoc, 0.0f);
            }
            if (matLoc >= 0) {
                glUniform1i(matLoc, partMaterialType);
            }
            glUniform3fv(colLoc, 1, partColor);
            part.mesh.draw();
            drawCount++;
        }
    }
    if (drawCount > 0 && playerIndex == 0) {
        static int drawLogCounter = 0;
        if ((drawLogCounter++ % 60) == 0) {
            LOGI("[PlayerRig::draw] P%d drawCount=%d worldPos=(%.2f,%.2f,%.2f)",
                 playerIndex, drawCount, playerWorld[0], playerWorld[1], playerWorld[2]);
        }
    }
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
uniform sampler2D u_BaseTexture;
uniform float u_UseTexture;
uniform float u_Time;
uniform int u_MaterialType;

// FIFA-quality PBR helpers
float D_GGX(float NoH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom);
}
float V_SmithGGX(float NoV, float NoL, float roughness) {
    float a = roughness * roughness;
    float ggxV = NoV * (1.0 - a) + a;
    float ggxL = NoL * (1.0 - a) + a;
    return 0.5 / (ggxV + ggxL);
}
vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

void main() {
    vec3 N = normalize(v_Normal);
    vec3 V = normalize(-v_WorldPos);
    float NoV = max(dot(N, V), 0.0);

    // ─── Base color & material detection ─────────────────────────
    vec3 baseColor = u_Color;
    float roughness = 0.75;   // default: fabric/kit
    float metallic = 0.0;
    float sss = 0.0;          // subsurface scattering

    if (u_UseTexture > 0.5) {
        vec3 texColor = texture(u_BaseTexture, v_TexCoord).rgb;
        baseColor = texColor * u_Color;
    }

    if (u_MaterialType == 1) { // Skin/Face/Head
        roughness = 0.45;
        sss = 0.60; // skin subsurface
    } else if (u_MaterialType == 2 || u_MaterialType == 3) { // Hair / Beard
        roughness = 0.35; // high spec hair
    }

    // ─── Lights ────────────────────────────────────────────────
    vec3 sunDir = normalize(vec3(0.35, 0.85, 0.45));
    vec3 sunColor = vec3(1.10, 1.02, 0.94);
    vec3 fillDir = normalize(vec3(-0.4, 0.3, -0.2));
    vec3 fillColor = vec3(0.35, 0.40, 0.52);

    // Spotlights (4 corners)
    vec3 spotDirs[4] = vec3[](
        normalize(vec3( 30.0, 20.0,  20.0)),
        normalize(vec3(-30.0, 20.0,  20.0)),
        normalize(vec3( 30.0, 20.0, -20.0)),
        normalize(vec3(-30.0, 20.0, -20.0))
    );
    vec3 spotColor = vec3(1.0, 0.95, 0.85) * 0.55;

    // ─── PBR shading per light ─────────────────────────────────
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 diffuse = baseColor * (1.0 - metallic);
    vec3 lit = vec3(0.0);

    // Sun
    {
        vec3 L = sunDir;
        vec3 H = normalize(L + V);
        float NoL = max(dot(N, L), 0.0);
        float NoH = max(dot(N, H), 0.0);
        float D = D_GGX(NoH, roughness);
        float Vis = V_SmithGGX(NoV, NoL, roughness);
        vec3 F = F_Schlick(max(dot(H, V), 0.0), F0);
        vec3 spec = D * Vis * F;
        vec3 kD = (1.0 - F) * (1.0 - metallic);
        // Skin subsurface: warp normal toward light
        vec3 sssN = normalize(mix(N, L, sss * 0.35));
        float sssDiff = max(dot(sssN, L), 0.0);
        lit += (kD * diffuse * mix(NoL, sssDiff, sss) + spec) * sunColor * 1.15;
    }

    // Fill (sky)
    {
        vec3 L = fillDir;
        float NoL = max(dot(N, L), 0.0);
        lit += diffuse * NoL * fillColor * 0.50;
    }
    // Spots (simplified)
    for (int i = 0; i < 4; i++) {
        float NoL = max(dot(N, spotDirs[i]), 0.0);
        lit += diffuse * NoL * spotColor * 0.15;
    }

    // Ambient / IBL approximation
    vec3 ambient = diffuse * 0.22;
    // Horizon boost: brighter near top (sky), darker near bottom (ground bounce)
    float horizon = N.y * 0.5 + 0.5;
    ambient *= mix(vec3(0.65, 0.70, 0.75), vec3(1.0, 0.98, 0.94), horizon);
    lit += ambient;

    // ─── Rim / Fresnel ─────────────────────────────────────────
    float rim = pow(1.0 - NoV, 4.0);
    vec3 rimColor = vec3(0.25, 0.30, 0.40) * rim * 0.4;
    // Sweat sheen on skin (thin oily layer)
    float sheen = pow(1.0 - NoV, 6.0) * sss * 0.5;
    rimColor += vec3(0.9, 0.85, 0.75) * sheen;

    // ─── Volumetric spot glow ────────────────────────────────
    float volumeGlow = 0.0;
    vec3 towerPositions[4] = vec3[](
        vec3( 10.0, 3.80,  8.0), vec3(-10.0, 3.80,  8.0),
        vec3( 10.0, 3.80, -8.0), vec3(-10.0, 3.80, -8.0)
    );
    for (int i = 0; i < 4; i++) {
        vec3 toFrag = v_WorldPos - towerPositions[i];
        float dist = length(toFrag);
        vec3 coneDir = normalize(vec3(0.0, -0.4, 0.0) - towerPositions[i]);
        float coneAngle = dot(normalize(toFrag), coneDir);
        if (coneAngle > 0.82) {
            float intensity = pow((coneAngle - 0.82) / 0.18, 2.5);
            volumeGlow += (intensity * 0.07) / (1.0 + dist * 0.25);
        }
    }

    vec3 finalColor = lit + rimColor + vec3(volumeGlow) * vec3(1.0, 0.93, 0.82);

    // ─── Filmic tone mapping (ACES approx) ───────────────────
    vec3 a = vec3(2.51, 2.51, 2.51);
    vec3 b = vec3(0.03, 0.03, 0.03);
    vec3 c = vec3(2.43, 2.43, 2.43);
    vec3 d = vec3(0.59, 0.59, 0.59);
    vec3 e = vec3(0.14, 0.14, 0.14);
    vec3 outCol = clamp((finalColor * (a * finalColor + b)) / (finalColor * (c * finalColor + d) + e), 0.0, 1.0);

    // ─── Bloom (extract bright areas, nice soft glow without brightening) ─────────
    vec3 bloom = max(outCol - vec3(0.60), vec3(0.0)) * vec3(0.25, 0.20, 0.14);
    outCol = outCol * 0.92 + bloom;

    // ─── Vignette + chromatic aberration hint ─────────────────
    // (only in post, not per-object, skip for performance)

    // ─── Film grain ──────────────────────────────────────────
    float grain = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453 + u_Time * 30.0) - 0.5;
    outCol += grain * 0.015;

    // Slight saturation boost for TV broadcast look
    float lum = dot(outCol, vec3(0.299, 0.587, 0.114));
    outCol = mix(vec3(lum), outCol, 1.34);

    outColor = vec4(outCol, 1.0);
}
)";

static const char* STATIC_VERT = R"(#version 300 es
precision mediump float;
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
uniform mat4 u_ModelViewProj;
uniform mat4 u_ModelMatrix;
uniform mat4 u_LightSpaceMatrix;
out vec3 v_Normal;
out vec2 v_TexCoord;
out vec3 v_LocalPos;
out vec3 v_WorldPos;
out vec4 v_ShadowCoord;
void main() {
    v_Normal = mat3(u_ModelMatrix) * a_Normal;
    v_TexCoord = a_TexCoord;
    v_LocalPos = a_Position;
    vec4 worldPos = u_ModelMatrix * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;
    v_ShadowCoord = u_LightSpaceMatrix * worldPos;
    gl_Position = u_ModelViewProj * vec4(a_Position, 1.0);
}
)";

static const char* STATIC_FRAG = R"(#version 300 es
precision mediump float;
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_LocalPos;
in vec3 v_WorldPos;
in vec4 v_ShadowCoord;
out vec4 outColor;
uniform vec3 u_Color;
uniform int u_MaterialType; // 0=default, 1=pitch, 2=goal, 3=ball, 4=stadium
uniform sampler2D u_BaseTexture;
uniform sampler2D u_OverlayTexture;
uniform float u_UseTexture; // 0.0 = color only, 1.0 = texture * color
uniform float u_UseOverlay; // 0.0 = no overlay, 1.0 = overlay * base
uniform vec2 u_PitchHalf;   // pitch half-extents in local space (meters)
uniform sampler2D u_ShadowMap;
uniform float u_FogDensity;

uniform sampler2D u_AdboardTex0;
uniform sampler2D u_AdboardTex1;
uniform sampler2D u_AdboardTex2;
uniform sampler2D u_AdboardTex3;

uniform float u_Time;

vec3 grassPitch() {
    vec3 baseGreen = vec3(0.24, 0.44, 0.16); // slightly deeper/richer green, less neon/washed out
    vec3 darkGreen = vec3(0.20, 0.38, 0.13); // closer to baseGreen for subtler stripes
    vec3 wornGreen = vec3(0.35, 0.44, 0.24); // trampled grass near center
    vec3 lineWhite = vec3(0.95, 0.95, 0.92);

    vec2 p = v_LocalPos.xz;
    vec2 n = p / max(u_PitchHalf, vec2(0.001));

    // Mowing stripes — very subtle so they never look like repeating tiles
    float stripeFreq = n.x * 8.0;
    float stripeVal = fract(stripeFreq);
    float fw = fwidth(stripeFreq);
    float stripe = smoothstep(0.5 - fw, 0.5 + fw, stripeVal);
    vec3 grass = mix(baseGreen, darkGreen, stripe * 0.30); // 30% contrast only

    // ─── Procedural grass blade noise + micro-irregularity (breaks up any remaining moiré) ───
    float hash = fract(sin(dot(floor(p * 40.0), vec2(12.9898, 78.233))) * 43758.5453);
    float bladeNoise = (hash - 0.5) * 0.012;
    grass += bladeNoise;

    // Extra micro-noise at higher frequency to completely destroy regular aliasing circles
    float microHash = fract(sin(dot(floor(p * 600.0), vec2(93.9898, 37.233))) * 12345.6789);
    grass += (microHash - 0.5) * 0.006;

    // ─── Wear patterns (center circle + penalty boxes get trampled) ───
    float rC = length(p);
    float centerWear = smoothstep(12.0, 0.0, rC) * 0.08;
    float boxWear = 0.0;
    if (abs(p.x) > 36.0 && abs(p.y) < 20.15) {
        boxWear += smoothstep(0.0, 5.0, 20.15 - abs(p.y)) * 0.06;
    }
    grass = mix(grass, wornGreen, centerWear + boxWear);

    // Very subtle high-frequency noise (no floor = no visible square blocks)
    float fineHash = fract(sin(dot(p * 3.7 + vec2(1.3, 2.1), vec2(17.13, 43.79))) * 23147.1);
    grass += (fineHash - 0.5) * 0.015;

    // Pitch markings — standard FIFA dimensions in meters
    float lineW = 0.55; // slightly thinner for broadcast
    float line = 0.0;

    // Touch lines (z = +/- 34.0)
    line = max(line, 1.0 - smoothstep(0.0, lineW, abs(abs(p.y) - 34.0)));
    // Goal lines (x = +/- 52.5)
    line = max(line, 1.0 - smoothstep(0.0, lineW, abs(abs(p.x) - 52.5)));
    // Half-way line (x = 0)
    line = max(line, 1.0 - smoothstep(0.0, lineW, abs(p.x)));
    // Center circle (r = 9.15)
    float circ = smoothstep(8.75, 9.15, rC) * (1.0 - smoothstep(9.15, 9.45, rC));
    line = max(line, circ);
    // Center spot
    line = max(line, 1.0 - smoothstep(0.0, 0.30, rC));
    // Penalty areas
    float inBoxX = step(36.0, abs(p.x));
    float inBoxZ = step(20.15, abs(p.y));
    if (inBoxX > 0.5 && inBoxZ < 0.5) {
        line = max(line, 1.0 - smoothstep(0.0, lineW, abs(abs(p.x) - 36.0)));
        line = max(line, 1.0 - smoothstep(0.0, lineW, abs(abs(p.y) - 20.15)));
    }
    // Goal areas
    if (abs(p.x) > 47.0 && abs(p.y) < 9.15) {
        line = max(line, 1.0 - smoothstep(0.0, lineW, abs(abs(p.x) - 47.0)));
        line = max(line, 1.0 - smoothstep(0.0, lineW, abs(abs(p.y) - 9.15)));
    }
    // Penalty spot
    if (abs(p.x) > 36.0) {
        float spotDist = length(vec2(abs(p.x) - 47.0, p.y));
        line = max(line, 1.0 - smoothstep(0.0, 0.30, spotDist));
    }

    grass = mix(grass, lineWhite, line);
    return grass;
}

// Procedural grass normal perturbation for anisotropic lighting
vec3 grassNormal() {
    vec2 p = v_LocalPos.xz;
    float nx = sin(p.x * 45.0 + p.y * 33.0) * 0.15;
    float ny = sin(p.x * 67.0 - p.y * 51.0 + 2.0) * 0.10;
    return normalize(v_Normal + vec3(nx, 0.0, ny));
}

vec3 ballPattern() {
    // FIFA-quality procedural soccer ball: truncated icosahedron + leather grain
    vec3 lp = v_LocalPos;
    // Pentagons
    float penta = sin(lp.x * 8.0) * sin(lp.y * 8.0) * sin(lp.z * 8.0);
    float pentaMask = smoothstep(0.10, 0.35, penta);
    // Hexagons (offset grid)
    float hexa = sin(lp.x * 12.0 + 1.0) * sin(lp.y * 12.0 + 2.0) * sin(lp.z * 12.0 + 3.0);
    float hexaMask = smoothstep(0.15, 0.45, hexa);
    float panel = max(pentaMask, hexaMask);

    // Leather grain bump (micro-noise)
    float grain = fract(sin(dot(lp.xy, vec2(43.12, 17.89))) * 12345.67) * 0.5
                + fract(sin(dot(lp.yz, vec2(12.98, 78.23))) * 43758.54) * 0.5;

    vec3 white = vec3(0.96, 0.96, 0.94) + grain * 0.02;
    vec3 black = vec3(0.06, 0.06, 0.06) + grain * 0.01;
    return mix(black, white, panel);
}

vec3 stadiumShader() {
    // Height Y boundaries from stadium_test.glb:
    //  - Y > 15.0: Tower-top floodlights (blazing emissive glow)
    //  - 10.0 < Y <= 15.0: Roof structures and supporting trusses
    //  - 6.6 < Y <= 10.0: Upper promenade and walkways
    //  - 1.0 < Y <= 6.6: Seating stands (tiers of colored blocks)
    //  - Y <= 1.0: Pitch apron and advertising boards

    if (v_LocalPos.y > 15.0) {
        // Warm white floodlight glow (reduced to avoid excessive bloom)
        return vec3(1.2, 1.1, 0.85);
    } 
    
    if (v_LocalPos.y > 10.0) {
        // Roof panels and steel structural trusses
        float trussX = step(0.90, fract(v_LocalPos.x * 0.5));
        float trussZ = step(0.90, fract(v_LocalPos.z * 0.5));
        vec3 steelFrame = vec3(0.22, 0.25, 0.28);
        vec3 canvasPanel = vec3(0.88, 0.88, 0.84);
        return mix(canvasPanel, steelFrame, clamp(trussX + trussZ, 0.0, 1.0));
    } 
    
    if (v_LocalPos.y > 6.6) {
        // Promenade stairs and concrete walls
        float steps = step(0.5, fract(v_LocalPos.y * 3.0));
        vec3 concrete = vec3(0.42, 0.44, 0.46);
        return concrete * mix(0.92, 1.08, steps);
    } 
    
    if (v_LocalPos.y > 1.0) {
        // Seating stands: FIFA-quality procedural animated crowd
        float angle = atan(v_LocalPos.z, v_LocalPos.x);
        float radius = length(v_LocalPos.xz);
        float seatRow = fract(v_LocalPos.y * 2.4);
        float seatBlock = step(0.25, sin(angle * 32.0));

        // National colour fan sections with variation
        float section = floor(angle * 6.0 / 3.14159);
        vec3 red    = vec3(0.75, 0.08, 0.08);
        vec3 green  = vec3(0.08, 0.55, 0.12);
        vec3 white  = vec3(0.92, 0.92, 0.90);
        vec3 blue   = vec3(0.08, 0.20, 0.65);
        vec3 yellow = vec3(0.90, 0.75, 0.05);
        float m = mod(section, 5.0);
        vec3 seatCol = (m < 1.0) ? red : (m < 2.0) ? green : (m < 3.0) ? white : (m < 4.0) ? blue : yellow;

        // Per-seat hash for individual spectators
        float seatHash = fract(sin(dot(floor(v_LocalPos.xz * 22.0), vec2(12.9898, 78.233))) * 43758.5453);
        float seatNoise = step(0.30, seatHash);
        float rowLight = step(0.5, seatRow);
        vec3 concreteStairs = vec3(0.34, 0.36, 0.40);
        vec3 seats = mix(concreteStairs, seatCol * (0.72 + rowLight * 0.28), seatBlock * seatNoise);

        // Animated crowd cheering — synchronized waves per section
        float cheer = sin(section * 2.5 + u_Time * 3.0) * 0.5 + 0.5;
        cheer = pow(cheer, 3.0);
        float standUp = step(0.85, seatHash) * cheer;
        seats += vec3(0.30, 0.28, 0.22) * standUp * seatBlock;

        // Phone flashlight effect (scattered bright dots when cheering peaks)
        float flashlight = step(0.97, seatHash) * step(0.6, cheer);
        seats += vec3(0.80, 0.75, 0.60) * flashlight * seatBlock;

        return seats;
    } 
    
    // Pitch apron and advertising panels (Y <= 1.0)
    if (v_LocalPos.y > 0.15) {
        float x = v_LocalPos.x;
        float y = v_LocalPos.y;
        float t = u_Time * 1.2;
        float panelIndex = floor(x * 0.08);
        float px = fract(x * 0.08);
        float py = (y - 0.15) / 0.85;

        // LED dot grid
        float ledGrid = step(0.12, fract(px * 120.0)) * step(0.12, fract(py * 20.0));
        vec3 ledOff = vec3(0.02, 0.02, 0.03);
        vec3 ledColor = ledOff;

        // 4 club brands rotating
        int brand = int(mod(panelIndex + floor(u_Time * 0.12), 4.0));

        vec3 texColor = vec3(0.0);
        if (brand == 0) {
            texColor = texture(u_AdboardTex0, vec2(px, 1.0 - py)).rgb;
        } else if (brand == 1) {
            texColor = texture(u_AdboardTex1, vec2(px, 1.0 - py)).rgb;
        } else if (brand == 2) {
            texColor = texture(u_AdboardTex2, vec2(px, 1.0 - py)).rgb;
        } else {
            texColor = texture(u_AdboardTex3, vec2(px, 1.0 - py)).rgb;
        }

        vec3 on = texColor * (0.85 + 0.15 * ledGrid);
        ledColor = mix(ledOff, on, 0.96);
        // Beautiful bright emissive LED display
        return ledColor * 1.25;
    }
    
    return vec3(0.16, 0.18, 0.20); // Gravel/dark concrete ground around the pitch
}

vec4 goalShader(vec3 lightColor) {
    // Goal line is at abs(x) = 52.5. Anything behind 52.65 is the net.
    float isNet = step(52.65, abs(v_LocalPos.x));

    if (isNet > 0.5) {
        // Net: draw a grid of square netting using UVs
        vec2 grid = abs(sin(v_TexCoord * 140.0));
        float netLine = step(0.92, max(grid.x, grid.y));
        float alpha = mix(0.12, 0.90, netLine);
        return vec4(vec3(0.95, 0.95, 0.95) * lightColor, alpha);
    } else {
        // Solid white posts and crossbars
        return vec4(vec3(0.98, 0.98, 0.98) * lightColor, 1.0);
    }
}

float sampleShadow(vec4 shadowCoord) {
    vec3 proj = shadowCoord.xyz / shadowCoord.w * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;

    // 3x3 PCF with 4096 shadow map for crisp, professional player silhouettes
    // Unpack depth from RG channels (16-bit precision)
    vec2 texelSize = vec2(1.0 / 4096.0);
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            vec4 samp = texture(u_ShadowMap, proj.xy + offset);
            float closest = samp.r + samp.g / 255.0;
            shadow += (proj.z > closest + 0.0003) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

vec3 acesTonemap(vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

vec3 applyFog(vec3 color, float dist) {
    vec3 fogColor = vec3(0.62, 0.66, 0.72); // slightly warmer, brighter atmospheric fog
    float fogAmount = 1.0 - exp(-dist * u_FogDensity);
    return mix(color, fogColor, fogAmount);
}

void main() {
    vec3 N = normalize(v_Normal);
    vec3 viewDir = normalize(-v_WorldPos);
    float NoV = max(dot(N, viewDir), 0.0);

    // ─── Lights ─────────────────────────────────────────────────
    vec3 L1 = normalize(vec3(0.35, 0.85, 0.45));
    vec3 sunColor = vec3(1.10, 1.02, 0.94);
    vec3 L2 = normalize(vec3(-0.4, 0.3, -0.2));
    vec3 fillColor = vec3(0.35, 0.40, 0.52);

    vec3 spotDirs[4] = vec3[](
        normalize(vec3( 30.0, 20.0,  20.0)),
        normalize(vec3(-30.0, 20.0,  20.0)),
        normalize(vec3( 30.0, 20.0, -20.0)),
        normalize(vec3(-30.0, 20.0, -20.0))
    );
    vec3 spotColor = vec3(1.0, 0.95, 0.85) * 0.5;

    // ─── Material setup ─────────────────────────────────────────
    vec3 baseColor;
    float alpha = 1.0;
    float roughness = 0.75;
    float metallic = 0.0;
    float specPower = 32.0;
    float specIntensity = 0.5;

    if (u_MaterialType == 1) {
        // Terrain: blend procedural grass with highly-tiled seamless grass texture for realistic micro-details
        N = grassNormal();
        vec3 proceduralGrass = grassPitch();
        if (u_UseTexture > 0.5) {
            vec3 texColor = texture(u_BaseTexture, v_TexCoord * 24.0).rgb;
            baseColor = proceduralGrass * mix(vec3(1.0), texColor * 1.4, 0.35);
        } else {
            baseColor = proceduralGrass;
        }
        roughness = 0.95; // matte grass
        metallic = 0.0;
        specPower = 8.0;
        specIntensity = 0.15;

        // Roof + floodlight shadow
        if (u_UseOverlay > 0.5) {
            // Compute non-tiling UV coordinates mapping exactly once over the pitch [0, 1]
            vec2 pitchUV = v_LocalPos.xz / (u_PitchHalf * 2.0) + 0.5;
            vec2 overlayUV = vec2(pitchUV.x, 1.0 - pitchUV.y);
            vec4 overlaySample = texture(u_OverlayTexture, overlayUV);
            // Filter out blurry low-resolution white pitch lines from overlay.png to avoid double lines.
            // Keep dark shadows and AO, but discard bright white pixels representing overlay lines.
            float isOverlayLine = step(0.55, overlaySample.r) * step(0.55, overlaySample.g) * step(0.55, overlaySample.b);
            baseColor = mix(baseColor, overlaySample.rgb, overlaySample.a * (1.0 - isOverlayLine));
        } else {
            float roofShadow = smoothstep(-8.0, 0.0, v_WorldPos.z) * smoothstep(8.0, 0.0, v_WorldPos.z);
            roofShadow *= smoothstep(-12.0, -4.0, v_WorldPos.x) * 0.35;
            float floodShadow = 0.0;
            for (int i = 0; i < 4; i++) {
                vec3 tpos = vec3((i < 2 ? 10.0 : -10.0), 3.8, (i == 0 || i == 2) ? 8.0 : -8.0);
                float d = length(v_WorldPos.xz - tpos.xz);
                floodShadow += smoothstep(18.0, 6.0, d) * 0.15;
            }
            float shadowMul = 1.0 - clamp(roofShadow + floodShadow, 0.0, 0.55);
            baseColor *= shadowMul;
        }
    } else if (u_MaterialType == 2) {
        vec3 gLight = vec3(0.5); // simplified light for goal
        vec4 g = goalShader(gLight);
        baseColor = g.rgb;
        alpha = g.a;
        roughness = 0.3;
        specPower = 64.0;
        specIntensity = 0.8;
    } else if (u_MaterialType == 3) {
        // Ball: leather material
        if (u_UseTexture > 0.5) {
            baseColor = texture(u_BaseTexture, v_TexCoord).rgb;
        } else {
            baseColor = ballPattern();
        }
        roughness = 0.35; // shiny leather
        metallic = 0.0;
        specPower = 48.0;
        specIntensity = 0.6;
    } else if (u_MaterialType == 5) {
        // Player skin / face / head
        if (u_UseTexture > 0.5) {
            baseColor = texture(u_BaseTexture, v_TexCoord).rgb;
        } else {
            baseColor = u_Color;
        }
        roughness = 0.45;
        specPower = 24.0;
        specIntensity = 0.35;
    } else if (u_MaterialType == 6) {
        // Player hair
        if (u_UseTexture > 0.5) {
            baseColor = texture(u_BaseTexture, v_TexCoord).rgb;
        } else {
            baseColor = u_Color;
        }
        roughness = 0.30;
        specPower = 48.0;
        specIntensity = 0.55;
    } else if (u_MaterialType == 7) {
        // Player beard (same as hair, slightly rougher)
        if (u_UseTexture > 0.5) {
            baseColor = texture(u_BaseTexture, v_TexCoord).rgb;
        } else {
            baseColor = u_Color;
        }
        roughness = 0.40;
        specPower = 32.0;
        specIntensity = 0.40;
    } else if (u_MaterialType == 4) {
        baseColor = stadiumShader();
        roughness = 0.9;
        specPower = 4.0;
        specIntensity = 0.1;
    } else if (u_UseTexture > 0.5) {
        vec3 texColor = texture(u_BaseTexture, v_TexCoord).rgb;
        baseColor = texColor * u_Color;
    } else {
        baseColor = u_Color;
    }

    // ─── PBR-ish lighting ─────────────────────────────────────
    float diff1 = max(dot(N, L1), 0.0);
    float diff2 = max(dot(N, L2), 0.0);

    float spotDiff = 0.0;
    for (int i = 0; i < 4; i++) {
        spotDiff += max(dot(N, spotDirs[i]), 0.0) * 0.25;
    }

    float ledDiff = max(dot(N, normalize(vec3( 0.8, 0.1, 0.6))), 0.0) * 0.05
                  + max(dot(N, normalize(vec3(-0.8, 0.1, 0.6))), 0.0) * 0.05;

    float amb = 0.22; // lowered from 0.35
    float light = diff1 * 0.95 + diff2 * 0.22 + spotDiff * 0.30 + ledDiff + amb; // adjusted slightly to avoid overexposure

    // Shadow mapping (very strong contrast for visible player silhouettes)
    float shadow = sampleShadow(v_ShadowCoord);
    light = mix(light * 0.05, light, shadow);

    // Specular Blinn-Phong
    vec3 H = normalize(L1 + viewDir);
    float spec = pow(max(dot(N, H), 0.0), specPower) * specIntensity;

    // Rim
    float rim = pow(1.0 - NoV, 4.0) * 0.3;

    // Volumetric spotlight dust glow
    float volumeGlow = 0.0;
    vec3 towerPositions[4] = vec3[](
        vec3( 10.0, 3.80,  8.0), vec3(-10.0, 3.80,  8.0),
        vec3( 10.0, 3.80, -8.0), vec3(-10.0, 3.80, -8.0)
    );
    for (int i = 0; i < 4; i++) {
        vec3 toFrag = v_WorldPos - towerPositions[i];
        float dist = length(toFrag);
        vec3 coneDir = normalize(vec3(0.0, -0.4, 0.0) - towerPositions[i]);
        float coneAngle = dot(normalize(toFrag), coneDir);
        if (coneAngle > 0.82) {
            float intensity = pow((coneAngle - 0.82) / 0.18, 2.5);
            volumeGlow += (intensity * 0.03) / (1.0 + dist * 0.25);
        }
    }

    vec3 lit = baseColor * light + vec3(spec * shadow) + vec3(rim * 0.4) + vec3(volumeGlow) * vec3(1.0, 0.93, 0.82);

    // ─── Filmic tone mapping (ACES) ───────────────────────────
    vec3 tm = acesTonemap(lit);

    // ─── Bloom (extract bright areas, nice soft glow without brightening) ─────────
    vec3 bloom = max(tm - vec3(0.60), vec3(0.0)) * vec3(0.25, 0.20, 0.14);
    vec3 finalColor = tm * 0.92 + bloom;

    // ─── Vignette (screen-space) ────────────────────────────────
    // Approximate using world distance from center (works for all materials)
    float distFromCenter = length(v_WorldPos.xz) * 0.015;
    float vignette = 1.0 - smoothstep(0.4, 1.2, distFromCenter) * 0.35;
    finalColor *= vignette;

    // ─── Film grain + broadcast saturation ─────────────────────
    float grain = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453 + u_Time * 30.0) - 0.5;
    finalColor += grain * 0.012;
    float lum = dot(finalColor, vec3(0.299, 0.587, 0.114));
    finalColor = mix(vec3(lum), finalColor, 1.34);

    // ─── Fog ────────────────────────────────────────────────────
    finalColor = applyFog(finalColor, length(v_WorldPos));

    outColor = vec4(finalColor, alpha);
}
)";

// ─── Shadow shaders ───────────────────────────────────────────────

static const char* SHADOW_VERT = R"(#version 300 es
precision mediump float;
layout(location = 0) in vec3 a_Position;
uniform mat4 u_ModelViewProj;
void main() {
    gl_Position = u_ModelViewProj * vec4(a_Position, 1.0);
}
)";

static const char* SHADOW_FRAG = R"(#version 300 es
precision mediump float;
out vec4 outColor;
void main() {
    // Standard precise 16-bit depth encoding into 8-bit normalized RG channels
    float d = gl_FragCoord.z;
    float g = fract(d * 255.0);
    float r = d - g / 255.0;
    outColor = vec4(r, g, 0.0, 1.0);
}
)";

// ─── UI overlay shader (2D screen-space, no textures) ────────────
static const char* UI_VERT = R"(#version 300 es
precision mediump float;
layout(location = 0) in vec2 a_Position;
uniform mat4 u_Ortho;
void main() {
    gl_Position = u_Ortho * vec4(a_Position, 0.0, 1.0);
}
)";
static const char* UI_FRAG = R"(#version 300 es
precision mediump float;
out vec4 outColor;
uniform vec4 u_Color;
void main() {
    outColor = u_Color;
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

static void mat4LookAt(float* m, float eyeX, float eyeY, float eyeZ,
                       float centerX, float centerY, float centerZ,
                       float upX, float upY, float upZ) {
    float fx = centerX - eyeX, fy = centerY - eyeY, fz = centerZ - eyeZ;
    float fn = 1.0f / std::sqrt(fx*fx + fy*fy + fz*fz); fx *= fn; fy *= fn; fz *= fn;
    float sx = fy * upZ - fz * upY;
    float sy = fz * upX - fx * upZ;
    float sz = fx * upY - fy * upX;
    float sn = 1.0f / std::sqrt(sx*sx + sy*sy + sz*sz); sx *= sn; sy *= sn; sz *= sn;
    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;
    m[0] = sx; m[1] = ux; m[2] = -fx; m[3] = 0.0f;
    m[4] = sy; m[5] = uy; m[6] = -fy; m[7] = 0.0f;
    m[8] = sz; m[9] = uz; m[10] = -fz; m[11] = 0.0f;
    m[12] = -(sx*eyeX + sy*eyeY + sz*eyeZ);
    m[13] = -(ux*eyeX + uy*eyeY + uz*eyeZ);
    m[14] = fx*eyeX + fy*eyeY + fz*eyeZ;
    m[15] = 1.0f;
}

static void mat4Ortho(float* m, float left, float right, float bottom, float top, float near, float far) {
    mat4Identity(m);
    m[0] = 2.0f / (right - left);
    m[5] = 2.0f / (top - bottom);
    m[10] = -2.0f / (far - near);
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[14] = -(far + near) / (far - near);
}

static float clampFloat(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint8_t sanitizePlayerAnim(uint8_t animId, float speed, uint8_t role) {
    (void)speed;
    // Pass through GF anim IDs directly. Clamp to valid GLB clip range (0..16).
    if (animId >= dzfoot::ANIM_COUNT) return dzfoot::ANIM_IDLE;
    // Non-goalkeepers should never play GK-specific animations
    if (role != 0 && animId >= dzfoot::ANIM_GK_IDLE) {
        return dzfoot::ANIM_IDLE;
    }
    return animId;
}

static bool isLoopingAnim(uint8_t animId) {
    return animId == dzfoot::ANIM_IDLE ||
           animId == dzfoot::ANIM_WALK ||
           animId == dzfoot::ANIM_RUN ||
           animId == dzfoot::ANIM_SPRINT ||
           animId == dzfoot::ANIM_DRIBBLE ||
           animId == dzfoot::ANIM_GK_IDLE;
}

// ─── ARRenderer implementation ───────────────────────────────────

void ARRenderer::init() {
    double t0 = nowMs();
    cameraShader_ = Shader::compile(CAMERA_VERT, CAMERA_FRAG);
    skinnedShader_ = Shader::compile(SKINNED_VERT, SKINNED_FRAG);
    staticShader_ = Shader::compile(STATIC_VERT, STATIC_FRAG);
    shadowShader_ = Shader::compile(SHADOW_VERT, SHADOW_FRAG);
    uiShader_ = Shader::compile(UI_VERT, UI_FRAG);
    LOGI("[initTiming] Shaders: %.1f ms", nowMs() - t0);

    glGenBuffers(1, &uiVbo_);

    t0 = nowMs();
    pitchTex_ = loadAssetTexture("beta2/media/textures/pitch/seamlessgrass08.png");
    if (!pitchTex_) pitchTex_ = loadAssetTexture("beta2/media/textures/pitch/pitch_01.png");
    if (!pitchTex_) pitchTex_ = loadAssetTexture("beta2/media/textures/stadium/greenish_floor.png");
    ballTex_ = loadAssetTexture("beta2/media/objects/balls/ball.jpg");
    LOGI("[initTiming] ballTex: %.1f ms", nowMs() - t0); t0 = nowMs();
    stadiumTex_ = loadAssetTexture("beta2/media/textures/stadium/greenish_floor.png");
    LOGI("[initTiming] stadiumTex: %.1f ms", nowMs() - t0); t0 = nowMs();
    crowdTex_ = loadAssetTexture("beta2/media/textures/stadium/crowd01.png");
    LOGI("[initTiming] crowdTex: %.1f ms", nowMs() - t0); t0 = nowMs();
    goalnettingTex_ = loadAssetTexture("beta2/media/textures/stadium/goalnetting.png");
    LOGI("[initTiming] goalnettingTex: %.1f ms", nowMs() - t0); t0 = nowMs();
    pitchOverlayTex_ = loadAssetTexture("beta2/media/textures/pitch/overlay.png");
    if (pitchOverlayTex_) {
        glBindTexture(GL_TEXTURE_2D, pitchOverlayTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    LOGI("[initTiming] pitchOverlayTex: %.1f ms", nowMs() - t0); t0 = nowMs();

    adboardTex_[0] = loadAssetTexture("beta2/media/textures/adboards/ad_your_ad_here.png");
    adboardTex_[1] = loadAssetTexture("beta2/media/textures/adboards/ad_polygon01.png");
    adboardTex_[2] = loadAssetTexture("beta2/media/textures/adboards/ad_stark01.png");
    adboardTex_[3] = loadAssetTexture("beta2/media/textures/adboards/ad_3xblast01.png");
    LOGI("[initTiming] adboardTexs: %.1f ms", nowMs() - t0); t0 = nowMs();
    LOGI("Textures: pitch=%u ball=%u stadium=%u crowd=%u goalnetting=%u pitchOverlay=%u",
         pitchTex_, ballTex_, stadiumTex_, crowdTex_, goalnettingTex_, pitchOverlayTex_);

    // Load compiled directional animations from asset folder
    dirAnimBank_.load(gAssetManager, "directional_anims.bin");
    LOGI("[initTiming] directional_anims.bin: %.1f ms", nowMs() - t0); t0 = nowMs();

    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadPositions_), quadPositions_, GL_STATIC_DRAW);

    static const float uvs[8] = {0, 1, 1, 1, 0, 0, 1, 0};
    glGenBuffers(1, &uvVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, uvVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(uvs), uvs, GL_STATIC_DRAW);
    LOGI("[initTiming] VBO setup: %.1f ms", nowMs() - t0); t0 = nowMs();

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
        // MUST update pitchHalf_ so player scales match this fallback geometry.
        // loadCube(1.0f) makes a 1x1x1 cube; with the scales above it becomes 11 x 0.25 x 5.
        pitchHalf_[0] = 11.0f * 0.5f;  // half length
        pitchHalf_[1] = 5.0f  * 0.5f;  // half width
    } else {
        LOGI("Pitch half-extents (local bounding box meters): X=%.2f Z=%.2f", pitchHalf_[0], pitchHalf_[1]);
        // Override pitchHalf_ to exact true FIFA playfield markings (52.5m x 34.0m)
        // rather than using the raw GLB bounding box which includes external concrete borders.
        pitchHalf_[0] = 52.5f;
        pitchHalf_[1] = 34.0f;

        // Real pitch GLB is in meters, scale down to fit camera view
        scene_.nodes[pitch].local.scale[0] = 0.1f;
        scene_.nodes[pitch].local.scale[1] = 0.1f;
        scene_.nodes[pitch].local.scale[2] = 0.1f;
        constexpr float kEnvYHalf = 0.4306f; // exact GF pitchHalfH/Y_FIELD_SCALE = 36/83.6
        const float scaleX = pitchHalf_[0] * 0.1f;
        const float scaleZ = pitchHalf_[1] * 0.1f / kEnvYHalf;
        LOGI("Player scales derived from true FIFA pitch: scaleX=%.3f scaleZ=%.3f",
             scaleX, scaleZ);
    }
    LOGI("[initTiming] pitch.glb: %.1f ms", nowMs() - t0); t0 = nowMs();

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
    LOGI("[initTiming] goals.glb: %.1f ms", nowMs() - t0); t0 = nowMs();

    // 3. Ball
    int ball = scene_.addNode("ball", root);
    if (!loadStaticGLB("ball.glb", scene_.nodes[ball].staticMesh)) {
        LOGE("Could not load ball.glb, falling back to loadSphere");
        scene_.nodes[ball].staticMesh.loadSphere(0.25f, 12, 12);
        scene_.nodes[ball].local.position[1] = 0.25f;
    } else {
        // Ball: slightly enlarged scale for mobile visibility while keeping proportion
        // ball.glb is ~22cm diameter; 0.15 makes it readable on small screens
        scene_.nodes[ball].local.scale[0] = 0.15f;
        scene_.nodes[ball].local.scale[1] = 0.15f;
        scene_.nodes[ball].local.scale[2] = 0.15f;
    }
    LOGI("[initTiming] ball.glb: %.1f ms", nowMs() - t0); t0 = nowMs();

    // 4. Stadium
    int stadium = scene_.addNode("stadium", root);
    if (!loadStaticGLB("stadium_test.glb", scene_.nodes[stadium].staticMesh)) {
        LOGE("Could not load stadium_test.glb, falling back to invisible");
        scene_.nodes[stadium].visible = false;
    } else {
        LOGI("Stadium loaded successfully from stadium_test.glb");
        scene_.nodes[stadium].local.scale[0] = 0.1f;
        scene_.nodes[stadium].local.scale[1] = 0.1f;
        scene_.nodes[stadium].local.scale[2] = 0.1f;
        scene_.nodes[stadium].visible = true;
    }
    LOGI("[initTiming] stadium_test.glb: %.1f ms", nowMs() - t0); t0 = nowMs();

    // 5. Player Base rigid rig (preload first rig as fallback)
    if (!playerRigs_[0].load("player_base.glb")) {
        LOGE("Could not load player_base.glb rig!");
    }
    LOGI("[initTiming] player_base.glb: %.1f ms", nowMs() - t0); t0 = nowMs();

    // 6. Shadow map FBO
    // Use RGBA color texture for depth encoding (100% portable on mobile drivers)
    glGenFramebuffers(1, &shadowFbo_);
    glGenTextures(1, &shadowColorTex_);
    glBindTexture(GL_TEXTURE_2D, shadowColorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kShadowMapSize, kShadowMapSize,
                  0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, kShadowMapSize, kShadowMapSize,
                  0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, shadowColorTex_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuf);
    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("Shadow FBO incomplete: 0x%x", fboStatus);
    } else {
        LOGI("Shadow map FBO created (%dx%d)", kShadowMapSize, kShadowMapSize);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    LOGI("[initTiming] shadow FBO: %.1f ms", nowMs() - t0);

    scene_.update();
    LOGI("[initTiming] scene_.update: %.1f ms", nowMs() - t0);
}

void ARRenderer::destroy() {
    Shader::destroy(cameraShader_);
    Shader::destroy(skinnedShader_);
    Shader::destroy(staticShader_);
    Shader::destroy(shadowShader_);
    Shader::destroy(uiShader_);
    cameraShader_ = 0;
    skinnedShader_ = 0;
    staticShader_ = 0;
    shadowShader_ = 0;
    uiShader_ = 0;
    if (shadowFbo_) { glDeleteFramebuffers(1, &shadowFbo_); shadowFbo_ = 0; }
    if (shadowTex_) { glDeleteTextures(1, &shadowTex_); shadowTex_ = 0; }
    if (shadowColorTex_) { glDeleteTextures(1, &shadowColorTex_); shadowColorTex_ = 0; }
    if (quadVbo_) { glDeleteBuffers(1, &quadVbo_); quadVbo_ = 0; }
    if (uvVbo_) { glDeleteBuffers(1, &uvVbo_); uvVbo_ = 0; }
    if (uiVbo_) { glDeleteBuffers(1, &uiVbo_); uiVbo_ = 0; }
    if (pitchTex_) { glDeleteTextures(1, &pitchTex_); pitchTex_ = 0; }
    if (ballTex_) { glDeleteTextures(1, &ballTex_); ballTex_ = 0; }
    if (stadiumTex_) { glDeleteTextures(1, &stadiumTex_); stadiumTex_ = 0; }
    if (crowdTex_) { glDeleteTextures(1, &crowdTex_); crowdTex_ = 0; }
    if (goalnettingTex_) { glDeleteTextures(1, &goalnettingTex_); goalnettingTex_ = 0; }
    if (pitchOverlayTex_) { glDeleteTextures(1, &pitchOverlayTex_); pitchOverlayTex_ = 0; }
    for (int i = 0; i < 4; ++i) {
        if (adboardTex_[i]) { glDeleteTextures(1, &adboardTex_[i]); adboardTex_[i] = 0; }
    }
    for (int t = 0; t < 2; ++t) {
        if (teamKitTex_[t]) { glDeleteTextures(1, &teamKitTex_[t]); teamKitTex_[t] = 0; }
        if (teamShortTex_[t]) { glDeleteTextures(1, &teamShortTex_[t]); teamShortTex_[t] = 0; }
    }
    dirAnimBank_.unload();
    for (int r = 0; r < 25; ++r) playerRigs_[r].destroy();
    for (auto& node : scene_.nodes) {
        node.skinnedMesh.destroy();
        node.staticMesh.destroy();
    }
    scene_.nodes.clear();
}

void ARRenderer::setPlayerMesh(const SkinnedMesh& mesh) {
    // Store mesh in scene graph for later cloning per player
    int playerNode = scene_.addNode("playerBase", scene_.findNode("root"));
    scene_.nodes[playerNode].skinnedMesh = mesh;
    scene_.nodes[playerNode].useSkinning = true;
}

void ARRenderer::setMatchSetup(const dzfoot::MatchSetupPacket& setup) {
    setup_ = setup;
    hasSetup_ = true;

    // Generate procedural kit textures from team colors
    for (int t = 0; t < 2; ++t) {
        if (teamKitTex_[t]) { glDeleteTextures(1, &teamKitTex_[t]); teamKitTex_[t] = 0; }
        if (teamShortTex_[t]) { glDeleteTextures(1, &teamShortTex_[t]); teamShortTex_[t] = 0; }

        const uint8_t* c1 = (t == 0) ? setup.teamAColor1 : setup.teamBColor1;
        const uint8_t* c2 = (t == 0) ? setup.teamAColor2 : setup.teamBColor2;
        teamKitTex_[t] = generateTeamKitTexture(c1[0], c1[1], c1[2], c2[0], c2[1], c2[2], 0);
        // Shorts: solid primary color, slightly darker
        uint8_t sr = (uint8_t)(c1[0] * 0.85f);
        uint8_t sg = (uint8_t)(c1[1] * 0.85f);
        uint8_t sb = (uint8_t)(c1[2] * 0.85f);
        teamShortTex_[t] = generateTeamKitTexture(sr, sg, sb, sr, sg, sb, 0);
    }
    LOGI("Team kit textures generated: A=(%d,%d,%d)/(%d,%d,%d) B=(%d,%d,%d)/(%d,%d,%d)",
         setup.teamAColor1[0], setup.teamAColor1[1], setup.teamAColor1[2],
         setup.teamAColor2[0], setup.teamAColor2[1], setup.teamAColor2[2],
         setup.teamBColor1[0], setup.teamBColor1[1], setup.teamBColor1[2],
         setup.teamBColor2[0], setup.teamBColor2[1], setup.teamBColor2[2]);
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
                               const float* boneMatrices, int numBones,
                               const uint8_t* playerAnims, const float* playerVels,
                               const float* playerRotY,
                               const uint8_t* playerFlags, const uint8_t* playerTeams,
                               const uint8_t* playerRoles,
                               TouchController* ctrl, int screenW, int screenH) {
    bool tracked = ar.isMarkerTracked();
    ARPose anchorPose = ar.getMarkerAnchorPose();

    float fallbackAnchor[16];
    if (ar.getCameraMode() == CameraMode::AR && anchorPose.valid) {
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
            // Map GF env coords to 3D scene units derived from the ACTUAL pitch GLB
            // so players/ball always align with the visible white lines.
            constexpr float kEnvYHalf = 0.4306f; // exact GF pitchHalfH/Y_FIELD_SCALE = 36/83.6
            const float scaleX  = pitchHalf_[0] * 0.1f;
            const float scaleZ  = pitchHalf_[1] * 0.1f / kEnvYHalf;
            const float bx = clampFloat(ballPosition[0], -1.05f, 1.05f);
            const float bw = clampFloat(ballPosition[1], -0.50f, 0.50f);
            scene_.nodes[ballIdx].local.position[0] = bx * scaleX;
            scene_.nodes[ballIdx].local.position[1] = ballPosition[2] * 0.1f + 0.08f; // height
            scene_.nodes[ballIdx].local.position[2] = bw * scaleZ;                    // width (Z-axis matches server Y-axis)
        }
        
        scene_.update();
    }

    // ─── Shadow pass ──────────────────────────────────────────────
    // Build light-space matrix from sun direction (matches shader sunDir L1 = vec3(0.35, 0.85, 0.45))
    float lightView[16], lightProj[16];
    float ratio = pitchHalf_[0] / 52.5f;
    float eyeX = 17.5f * ratio;
    float eyeY = 42.5f * ratio;
    float eyeZ = 22.5f * ratio;
    mat4LookAt(lightView, eyeX, eyeY, eyeZ, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    
    // Tight orthographic shadow box: cover just the pitch + minimal margin
    // This concentrates all 4096 shadow-map pixels on the playable area
    // for much higher per-pixel resolution on player silhouettes (~5x better)
    float extentX = pitchHalf_[0] * 1.05f;
    float extentZ = pitchHalf_[1] * 1.05f;
    // Tight near/far planes improve depth precision (less shadow acne, visible foot shadows)
    // Far must cover distance from light to farthest pitch corner (~99m at ratio=1)
    // so the terrain is NOT clipped out of the shadow map.
    mat4Ortho(lightProj, -extentX, extentX, -extentZ, extentZ, 1.0f, 120.0f * ratio);
    mat4Mul(lightProj, lightView, lightSpaceMatrix_);

    renderShadowMap(playerPositions, numPlayers, playerAnims, playerVels, playerRotY,
                    playerFlags, playerTeams, playerRoles);

    // ─── Main pass ──────────────────────────────────────────────
    renderStaticObjects(vpa, lightSpaceMatrix_);
    renderPlayers(vpa, lightSpaceMatrix_, playerPositions, numPlayers,
                  playerAnims, playerVels, playerRotY, playerFlags, playerTeams, playerRoles);

    // ─── UI overlay ───────────────────────────────────────────────
    if (ctrl && screenW > 0 && screenH > 0) {
        renderUI(*ctrl, screenW, screenH, vpa, playerPositions, playerFlags, playerTeams);
    }
}

void ARRenderer::renderStaticObjects(const float* viewProj, const float* lightSpaceMatrix) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    Shader::use(staticShader_);
    GLint mvpLoc = glGetUniformLocation(staticShader_, "u_ModelViewProj");
    GLint modelLoc = glGetUniformLocation(staticShader_, "u_ModelMatrix");
    GLint colLoc = glGetUniformLocation(staticShader_, "u_Color");
    GLint matLoc = glGetUniformLocation(staticShader_, "u_MaterialType");
    GLint pitchHalfLoc = glGetUniformLocation(staticShader_, "u_PitchHalf");
    GLint useTexLoc = glGetUniformLocation(staticShader_, "u_UseTexture");
    GLint texLoc = glGetUniformLocation(staticShader_, "u_BaseTexture");
    GLint useOverlayLoc = glGetUniformLocation(staticShader_, "u_UseOverlay");
    GLint overlayTexLoc = glGetUniformLocation(staticShader_, "u_OverlayTexture");
    GLint timeLoc = glGetUniformLocation(staticShader_, "u_Time");
    GLint lightSpaceLoc = glGetUniformLocation(staticShader_, "u_LightSpaceMatrix");
    GLint shadowMapLoc = glGetUniformLocation(staticShader_, "u_ShadowMap");
    GLint fogDensityLoc = glGetUniformLocation(staticShader_, "u_FogDensity");

    glUniform2f(pitchHalfLoc, pitchHalf_[0], pitchHalf_[1]);
    glUniform1f(useTexLoc, 0.0f);
    glUniform1f(useOverlayLoc, 0.0f);
    if (timeLoc >= 0) {
        glUniform1f(timeLoc, static_cast<float>(nowMs() * 0.001));
    }
    if (lightSpaceLoc >= 0) {
        glUniformMatrix4fv(lightSpaceLoc, 1, GL_FALSE, lightSpaceMatrix);
    }
    if (fogDensityLoc >= 0) {
        glUniform1f(fogDensityLoc, 0.025f);
    }
    if (shadowMapLoc >= 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadowColorTex_);
        glUniform1i(shadowMapLoc, 1);
    }

    // Set adboard textures for stadium LED panels
    GLint adboardLocs[4];
    adboardLocs[0] = glGetUniformLocation(staticShader_, "u_AdboardTex0");
    adboardLocs[1] = glGetUniformLocation(staticShader_, "u_AdboardTex1");
    adboardLocs[2] = glGetUniformLocation(staticShader_, "u_AdboardTex2");
    adboardLocs[3] = glGetUniformLocation(staticShader_, "u_AdboardTex3");
    for (int i = 0; i < 4; ++i) {
        if (adboardLocs[i] >= 0) {
            glActiveTexture(GL_TEXTURE2 + i);
            // Fall back to stadiumTex_ or pitchTex_ if not loaded
            glBindTexture(GL_TEXTURE_2D, adboardTex_[i] ? adboardTex_[i] : (stadiumTex_ ? stadiumTex_ : pitchTex_));
            glUniform1i(adboardLocs[i], 2 + i);
        }
    }

    for (auto& node : scene_.nodes) {
        if (node.useSkinning || !node.visible) continue;
        if (!node.staticMesh.hasData()) continue;

        float mvp[16];
        mat4Mul(viewProj, node.worldMatrix, mvp);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);
        if (modelLoc >= 0) {
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, node.worldMatrix);
        }

        int materialType = 0;
        GLuint boundTex = 0;
        bool hasOverlay = false;
        if (node.name == "pitch") {
            glUniform3f(colLoc, 1.0f, 1.0f, 1.0f);
            materialType = 1; // pitch
            boundTex = pitchTex_;

            if (pitchOverlayTex_ && overlayTexLoc >= 0 && useOverlayLoc >= 0) {
                glActiveTexture(GL_TEXTURE6);
                glBindTexture(GL_TEXTURE_2D, pitchOverlayTex_);
                glUniform1i(overlayTexLoc, 6);
                glUniform1f(useOverlayLoc, 1.0f);
                hasOverlay = true;
            }
        } else if (node.name.find("goal") == 0) {
            glUniform3f(colLoc, 0.95f, 0.95f, 0.95f);
            materialType = 2;
        } else if (node.name == "ball") {
            glUniform3f(colLoc, 1.0f, 1.0f, 1.0f);
            materialType = 3; // ball uses texture (via shader) or procedural fallback
            boundTex = ballTex_;
        } else if (node.name == "stadium") {
            // Stadium is 100% procedural in shader (no GF crowd/stadium textures)
            glUniform3f(colLoc, 1.0f, 1.0f, 1.0f);
            materialType = 4;
        } else {
            glUniform3f(colLoc, 1.0f, 1.0f, 1.0f);
            materialType = 0;
        }
        glUniform1i(matLoc, materialType);
        if (boundTex && texLoc >= 0 && useTexLoc >= 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, boundTex);
            glUniform1i(texLoc, 0);
            glUniform1f(useTexLoc, 1.0f);
        } else if (useTexLoc >= 0) {
            glUniform1f(useTexLoc, 0.0f);
        }

        if (!hasOverlay && useOverlayLoc >= 0) {
            glUniform1f(useOverlayLoc, 0.0f);
        }

        node.staticMesh.draw();

        // Restore active texture slot and unbind overlay texture safely
        if (hasOverlay) {
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
    }

    glDisable(GL_BLEND);
}

void ARRenderer::renderPlayers(const float* viewProj, const float* lightSpaceMatrix,
                               const float* playerPositions, int numPlayers,
                               const uint8_t* playerAnims, const float* playerVels,
                               const float* playerRotY,
                               const uint8_t* playerFlags, const uint8_t* playerTeams,
                               const uint8_t* playerRoles) {
    // Check if at least one rig is loaded
    bool anyRigLoaded = false;
    for (int r = 0; r < 25; ++r) {
        if (!playerRigs_[r].nodes.empty()) { anyRigLoaded = true; break; }
    }
    if (!anyRigLoaded) {
        // fallback: draw tiny cubes if no rig loaded
        static int fallbackCounter = 0;
        bool shouldLog = (fallbackCounter++ % 120 == 0);
        if (shouldLog) LOGI("[ARRenderer::renderPlayers] No PlayerRig loaded, using cube fallback");

        Shader::use(staticShader_);
        GLint mvpLoc = glGetUniformLocation(staticShader_, "u_ModelViewProj");
        GLint modelLoc = glGetUniformLocation(staticShader_, "u_ModelMatrix");
        GLint colLoc = glGetUniformLocation(staticShader_, "u_Color");
        GLint lightSpaceLoc = glGetUniformLocation(staticShader_, "u_LightSpaceMatrix");
        GLint shadowMapLoc = glGetUniformLocation(staticShader_, "u_ShadowMap");
        GLint fogDensityLoc = glGetUniformLocation(staticShader_, "u_FogDensity");
        if (lightSpaceLoc >= 0) glUniformMatrix4fv(lightSpaceLoc, 1, GL_FALSE, lightSpaceMatrix);
        if (fogDensityLoc >= 0) glUniform1f(fogDensityLoc, 0.025f);
        if (shadowMapLoc >= 0) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, shadowColorTex_);
            glUniform1i(shadowMapLoc, 1);
        }
        // Map GF env coords to 3D scene units derived from the ACTUAL pitch GLB.
        constexpr float kEnvYHalf = 0.4306f; // exact GF pitchHalfH/Y_FIELD_SCALE = 36/83.6
        const float scaleX  = pitchHalf_[0] * 0.1f;
        const float scaleZ  = pitchHalf_[1] * 0.1f / kEnvYHalf;
        for (int i = 0; i < numPlayers; ++i) {
            // Skip non-active players (sent off / substituted)
            uint8_t flags = playerFlags ? playerFlags[i] : 0xFF;
            if (!(flags & 1)) continue;
            float gx = clampFloat(playerPositions[i * 3 + 0], -1.05f, 1.05f);
            float gw = clampFloat(playerPositions[i * 3 + 1], -0.50f, 0.50f);
            float gh = playerPositions[i * 3 + 2];
            float localModel[16] = {
                0.4f, 0, 0, 0,
                0, 0.4f, 0, 0,
                0, 0, 0.4f, 0,
                gx * scaleX, gh * 0.1f + 0.6f, gw * scaleZ, 1
            };
            float mvp[16];
            mat4Mul(viewProj, localModel, mvp);
            glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);
            if (modelLoc >= 0) glUniformMatrix4fv(modelLoc, 1, GL_FALSE, localModel);
            bool teamA = playerTeams ? (playerTeams[i] == 0) : (i < 11);
            glUniform3f(colLoc, teamA ? 0.0f : 1.0f, teamA ? 0.5f : 0.0f, teamA ? 1.0f : 0.0f);
        }
        return;
    }

    Shader::use(staticShader_);
    GLint mvpLoc = glGetUniformLocation(staticShader_, "u_ModelViewProj");
    GLint modelLoc = glGetUniformLocation(staticShader_, "u_ModelMatrix");
    GLint colLoc = glGetUniformLocation(staticShader_, "u_Color");
    GLint matLoc = glGetUniformLocation(staticShader_, "u_MaterialType");
    GLint useTexLoc = glGetUniformLocation(staticShader_, "u_UseTexture");
    GLint lightSpaceLoc = glGetUniformLocation(staticShader_, "u_LightSpaceMatrix");
    GLint shadowMapLoc = glGetUniformLocation(staticShader_, "u_ShadowMap");
    GLint fogDensityLoc = glGetUniformLocation(staticShader_, "u_FogDensity");
    GLint blobShadowLoc = glGetUniformLocation(staticShader_, "u_BlobShadow");
    if (blobShadowLoc >= 0) glUniform1f(blobShadowLoc, 0.0f);
    glUniform1i(matLoc, 0); // force default color mode so ballPattern is NOT used
    glUniform1f(useTexLoc, 0.0f);
    if (lightSpaceLoc >= 0) glUniformMatrix4fv(lightSpaceLoc, 1, GL_FALSE, lightSpaceMatrix);
    if (fogDensityLoc >= 0) glUniform1f(fogDensityLoc, 0.025f);
    if (shadowMapLoc >= 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadowTex_);
        glUniform1i(shadowMapLoc, 1);
    }

    static int playerFrameCounter = 0;
    bool shouldLog = (playerFrameCounter++ % 120 == 0);
    if (shouldLog) {
        size_t totalNodes = 0;
        for (int r = 0; r < 25; ++r) totalNodes += playerRigs_[r].nodes.size();
        LOGI("[renderPlayers] Total rig nodes=%zu, numPlayers=%d", totalNodes, numPlayers);
    }

    // GF env_coord ranges: X in [-1,1] (length), Y in [-0.43,0.43] (width)
    // Map to 3D scene units derived from the ACTUAL pitch GLB.
    constexpr float kEnvYHalf = 0.4306f; // exact GF pitchHalfH/Y_FIELD_SCALE = 36/83.6
    const float scaleX  = pitchHalf_[0] * 0.1f;
    const float scaleZ  = pitchHalf_[1] * 0.1f / kEnvYHalf;
    float pitchScale = pitchHalf_[0] / 52.5f;

    for (int i = 0; i < numPlayers; ++i) {
        // --- Exploit server flags ---
        uint8_t flags = playerFlags ? playerFlags[i] : 0xFF;
        // bit 0 = is_active: skip non-active players (sent off / substituted)
        if (!(flags & 1)) continue;

        float gx = clampFloat(playerPositions[i * 3 + 0], -1.05f, 1.05f);
        float gw = clampFloat(playerPositions[i * 3 + 1], -0.50f, 0.50f);
        float gh = playerPositions[i * 3 + 2];
        float worldPos[3] = { gx * scaleX, gh * 0.1f * pitchScale + 0.14f * pitchScale, gw * scaleZ };

        // Heading: Z-axis matches server Y-axis, so mapping is direct
        // playerVels[i*3+0..1] = unit direction vector (from server dir[])
        // playerVels[i*3+2]    = scalar speed (from server vel magnitude)
        float dirX = playerVels ? playerVels[i * 3 + 0] : 0.0f;
        float dirY = playerVels ? playerVels[i * 3 + 1] : 1.0f;
        float rotY = playerRotY ? playerRotY[i] : std::atan2(dirX, dirY);

        // Animation state for this player — USE EXACTLY what GF sends, no remapping
        uint8_t rawAnim = playerAnims ? playerAnims[i] : 0;
        uint8_t desiredAnim = rawAnim;
        float speed = playerVels ? playerVels[i * 3 + 2] : 0.0f;
        uint8_t role = playerRoles ? playerRoles[i] : 0;
        desiredAnim = sanitizePlayerAnim(desiredAnim, speed, role);

        // NO remapping — GF anim is the ground truth
        (void)flags; // flags still available if needed later

        if (playerAnims_[i].current != desiredAnim) {
            playerAnims_[i].play(desiredAnim);
        }
        playerAnims_[i].update(0.016f);

        // --- Directional Animation Selection ---
        // DISABLED: directional clips are compiled from GF .anim files but the
        // bone rotation/position data does not map correctly to the GLB skeleton.
        // Using embedded GLB animations only until the directional bank is verified.
        constexpr bool kUseExternalDirAnims = false;
        const DirAnimClip* dirClip = nullptr;
        float relAngleDeg = 0.0f;
        (void)relAngleDeg;
        if (kUseExternalDirAnims && !dirAnimBank_.empty()) {
            // moveAngle must be in same space as server rotY (atan2(dirX, dirY))
            float moveAngle = std::atan2(dirX, dirY);
            float relAngle = rotY - moveAngle;
            relAngle = std::fmod(std::abs(relAngle), 2.0f * 3.14159265f);
            if (relAngle > 3.14159265f) relAngle = 2.0f * 3.14159265f - relAngle;
            relAngleDeg = relAngle * 180.0f / 3.14159265f;

            DirectionalAnimBank::Query q;
            q.category = playerAnims_[i].current;
            q.relAngleDeg = relAngleDeg;
            if (speed < 0.0005f) q.velocityIn = 0; // idle
            else if (speed < 0.008f) q.velocityIn = 1; // walk
            else if (speed < 0.05f) q.velocityIn = 2; // run/dribble
            else q.velocityIn = 3; // sprint

            dirClip = dirAnimBank_.select(q);
        }

        // Team color from server (not index-based assumption)
        float teamColor[3] = {0.05f, 0.15f, 0.70f}; // default team A
        if (hasSetup_ && playerTeams) {
            if (playerTeams[i] == 0) {
                teamColor[0] = setup_.teamAColor1[0] / 255.0f;
                teamColor[1] = setup_.teamAColor1[1] / 255.0f;
                teamColor[2] = setup_.teamAColor1[2] / 255.0f;
            } else if (playerTeams[i] == 1) {
                teamColor[0] = setup_.teamBColor1[0] / 255.0f;
                teamColor[1] = setup_.teamBColor1[1] / 255.0f;
                teamColor[2] = setup_.teamBColor1[2] / 255.0f;
            } else if (playerTeams[i] == 2) {
                // Officials: neon yellow/green
                teamColor[0] = 0.85f; teamColor[1] = 0.85f; teamColor[2] = 0.02f;
            }
        } else if (playerTeams) {
            if (playerTeams[i] == 1) {
                // Team B: deep red
                teamColor[0] = 0.80f; teamColor[1] = 0.05f; teamColor[2] = 0.05f;
            } else if (playerTeams[i] == 2) {
                // Officials: neon yellow/green
                teamColor[0] = 0.85f; teamColor[1] = 0.85f; teamColor[2] = 0.02f;
            }
        } else {
            if (i >= 11) {
                // Fallback team B
                teamColor[0] = 0.80f; teamColor[1] = 0.05f; teamColor[2] = 0.05f;
            }
        }

        // bit 3 = has_possession: brighten the ball holder
        if (flags & 8) {
            teamColor[0] = std::min(teamColor[0] + 0.30f, 1.0f);
            teamColor[1] = std::min(teamColor[1] + 0.40f, 1.0f);
            teamColor[2] = std::min(teamColor[2] + 0.30f, 1.0f);
        }
        // bit 2 = designated_player: slight yellow tint
        if (flags & 4) {
            teamColor[0] = std::min(teamColor[0] + 0.15f, 1.0f);
            teamColor[1] = std::min(teamColor[1] + 0.15f, 1.0f);
        }

        // GF server normalization constants (X_FIELD_SCALE=54.4, Y_FIELD_SCALE=-83.6, Z_FIELD_SCALE=1)
        float gfMetersX = gx * 54.4f;
        float gfMetersY = gw * (-83.6f);
        float gfMetersZ = gh * 1.0f;

        // Logging removed to prevent ANR caused by logd saturation.
        // (void)gfMetersX; (void)gfMetersY; (void)gfMetersZ;

        // Build modular avatar config from MatchSetup packet
        AvatarConfig cfg;
        cfg.bodyType = 1; cfg.hairStyle = 0; cfg.beardStyle = 0;
        cfg.skinColor = 3; cfg.hairColor = 0; cfg.playerNumber = 0; cfg.height = 1.0f;
        if (hasSetup_ && i < 22) {
            const auto& p = setup_.players[i];
            cfg.bodyType     = (p.bodyType < 4)    ? p.bodyType    : 1;
            cfg.hairStyle    = (p.hairStyle < 6)   ? p.hairStyle   : 0;
            cfg.beardStyle   = (p.beardStyle < 4)  ? p.beardStyle  : 0;
            cfg.skinColor    = (p.skinColor < 7)   ? p.skinColor   : 3;
            cfg.hairColor    = (p.hairColor < 8)   ? p.hairColor   : 0;
            cfg.playerNumber = (p.playerNumber <= 99) ? p.playerNumber : 0;
            cfg.height       = (p.height > 0.5f && p.height < 2.5f) ? p.height : 1.0f;
        }

        // Lazy-load modular avatar per player index, or reload if config changed
        if (playerRigs_[i].nodes.empty() || !playerRigs_[i].configMatches(cfg)) {
            if (!playerRigs_[i].loadModular(cfg)) {
                // Fallback: copy from rig 0 if available
                if (!playerRigs_[0].nodes.empty()) {
                    // Can't easily copy PlayerRig (contains GPU resources), skip for now
                    LOGI("[renderPlayers] Player %d modular load failed, will use fallback", i);
                }
            } else {
                playerRigs_[i].loadedCfg_ = cfg;
                playerRigs_[i].hasLoadedCfg_ = true;
            }
        }

        // Use player's own rig if loaded, otherwise fall back to rig 0
        PlayerRig* rig = &playerRigs_[i];
        if (rig->nodes.empty()) rig = &playerRigs_[0];

        uint8_t pTeam = playerTeams ? playerTeams[i] : (i < 11 ? 0 : 1);
        GLuint playerKitTex = (pTeam < 2) ? teamKitTex_[pTeam] : 0;
        GLuint playerShortTex = (pTeam < 2) ? teamShortTex_[pTeam] : 0;
        rig->draw(viewProj, worldPos, rotY,
                  playerAnims_[i].current, playerAnims_[i].previous, playerAnims_[i].blend,
                  playerAnims_[i].time, playerAnims_[i].prevTime,
                  staticShader_, skinnedShader_, teamColor,
                  playerKitTex, playerShortTex,
                  i, dirClip, &cfg,
                  lightSpaceMatrix_, shadowColorTex_,
                  pitchHalf_[0] / 52.5f);

        if (shouldLog && i == 0) {
            LOGI("[renderPlayers] P%d num=%d drawn at (%.2f,%.2f,%.2f) rotY=%.2f kitTex=%u", i,
                 (int)cfg.playerNumber, worldPos[0], worldPos[1], worldPos[2], rotY, playerKitTex);
        }
    }
}

void ARRenderer::renderShadowMap(const float* playerPositions, int numPlayers,
                                 const uint8_t* playerAnims, const float* playerVels,
                                 const float* playerRotY,
                                 const uint8_t* playerFlags, const uint8_t* playerTeams,
                                 const uint8_t* playerRoles) {
    if (!shadowFbo_ || !shadowShader_) return;

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, kShadowMapSize, kShadowMapSize);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // Save previous clear color and clear shadow color texture to maximum depth (R=1.0, G=1.0)
    GLfloat prevClearColor[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClearColor);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2], prevClearColor[3]);

    Shader::use(shadowShader_);
    GLint mvpLoc = glGetUniformLocation(shadowShader_, "u_ModelViewProj");

    // Render static objects into shadow map (cull back faces: terrain face-up gets drawn)
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    for (auto& node : scene_.nodes) {
        if (node.useSkinning || !node.visible) continue;
        if (!node.staticMesh.hasData()) continue;
        if (node.name == "pitch" || node.name == "stadium") continue; // SKIP terrain and stadium (they don't cast shadows on the field)
        float mvp[16];
        mat4Mul(lightSpaceMatrix_, node.worldMatrix, mvp);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);
        node.staticMesh.draw();
    }

    // Render player dynamic characters into shadow map
    // Disable culling: player_base.glb uses open-ended limb meshes that create holes with GL_FRONT
    glDisable(GL_CULL_FACE);
    if (playerPositions) {
        constexpr float kEnvYHalf = 0.4306f; // exact GF pitchHalfH/Y_FIELD_SCALE = 36/83.6
        const float scaleX  = pitchHalf_[0] * 0.1f;
        const float scaleZ  = pitchHalf_[1] * 0.1f / kEnvYHalf;
        float pitchScale = pitchHalf_[0] / 52.5f;
        float teamColor[3] = {0.0f, 0.0f, 0.0f};

        for (int i = 0; i < numPlayers; ++i) {
            uint8_t flags = playerFlags ? playerFlags[i] : 0xFF;
            if (!(flags & 1)) continue;

            float gx = clampFloat(playerPositions[i * 3 + 0], -1.05f, 1.05f);
            float gw = clampFloat(playerPositions[i * 3 + 1], -0.50f, 0.50f);
            float gh = playerPositions[i * 3 + 2];
            float worldPos[3] = { gx * scaleX, gh * 0.1f * pitchScale + 0.14f * pitchScale, gw * scaleZ };

            float dirX = playerVels ? playerVels[i * 3 + 0] : 0.0f;
            float dirY = playerVels ? playerVels[i * 3 + 1] : 1.0f;
            float rotY = playerRotY ? playerRotY[i] : std::atan2(dirX, dirY);

            PlayerRig* rig = &playerRigs_[i];
            if (rig->nodes.empty()) rig = &playerRigs_[0];
            if (rig->nodes.empty()) continue;

            rig->draw(lightSpaceMatrix_, worldPos, rotY,
                      playerAnims_[i].current, playerAnims_[i].previous, playerAnims_[i].blend,
                      playerAnims_[i].time, playerAnims_[i].prevTime,
                      shadowShader_, shadowShader_, teamColor,
                      0, 0,
                      i, nullptr, nullptr,
                      nullptr, 0,
                      pitchHalf_[0] / 52.5f);
        }
    }

    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

void ARRenderer::renderUI(TouchController& ctrl, int screenW, int screenH,
                          const float* viewProj, const float* playerPositions,
                          const uint8_t* playerFlags, const uint8_t* playerTeams) {
    if (!uiShader_ || !uiVbo_) return;

    // Disable depth so UI draws over everything
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Orthographic projection: (0,0) top-left, (W,H) bottom-right
    float ortho[16] = {
        2.0f / screenW, 0, 0, 0,
        0, -2.0f / screenH, 0, 0,
        0, 0, 1, 0,
        -1, 1, 0, 1
    };

    Shader::use(uiShader_);
    GLint orthoLoc = glGetUniformLocation(uiShader_, "u_Ortho");
    GLint colorLoc = glGetUniformLocation(uiShader_, "u_Color");
    glUniformMatrix4fv(orthoLoc, 1, GL_FALSE, ortho);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    auto drawCircle = [&](float cx, float cy, float r, uint32_t rgba, int segs = 32) {
        std::vector<float> verts;
        verts.reserve((segs + 2) * 2);
        verts.push_back(cx); verts.push_back(cy);
        for (int i = 0; i <= segs; ++i) {
            float ang = i * 6.2831853f / segs;
            verts.push_back(cx + std::cos(ang) * r);
            verts.push_back(cy + std::sin(ang) * r);
        }
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STREAM_DRAW);
        float a = ((rgba >> 24) & 0xFF) / 255.0f;
        float rcol = ((rgba >> 16) & 0xFF) / 255.0f;
        float g = ((rgba >> 8) & 0xFF) / 255.0f;
        float b = ((rgba) & 0xFF) / 255.0f;
        glUniform4f(colorLoc, rcol, g, b, a);
        glDrawArrays(GL_TRIANGLE_FAN, 0, segs + 2);
    };

    auto drawRing = [&](float cx, float cy, float r, float thickness, uint32_t rgba, int segs = 32) {
        std::vector<float> verts;
        verts.reserve((segs + 1) * 4);
        for (int i = 0; i <= segs; ++i) {
            float ang = i * 6.2831853f / segs;
            float c = std::cos(ang), s = std::sin(ang);
            verts.push_back(cx + c * (r + thickness)); verts.push_back(cy + s * (r + thickness));
            verts.push_back(cx + c * (r - thickness)); verts.push_back(cy + s * (r - thickness));
        }
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STREAM_DRAW);
        float a = ((rgba >> 24) & 0xFF) / 255.0f;
        float rcol = ((rgba >> 16) & 0xFF) / 255.0f;
        float g = ((rgba >> 8) & 0xFF) / 255.0f;
        float b = ((rgba) & 0xFF) / 255.0f;
        glUniform4f(colorLoc, rcol, g, b, a);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, (segs + 1) * 2);
    };

    auto drawLine = [&](float x1, float y1, float x2, float y2, float thickness, uint32_t rgba) {
        float dx = x2 - x1, dy = y2 - y1;
        float len = std::sqrt(dx*dx + dy*dy);
        if (len < 0.001f) return;
        float nx = -dy / len * thickness, ny = dx / len * thickness;
        float verts[8] = {
            x1 + nx, y1 + ny, x1 - nx, y1 - ny,
            x2 + nx, y2 + ny, x2 - nx, y2 - ny
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
        float a = ((rgba >> 24) & 0xFF) / 255.0f;
        float rcol = ((rgba >> 16) & 0xFF) / 255.0f;
        float g = ((rgba >> 8) & 0xFF) / 255.0f;
        float b = ((rgba) & 0xFF) / 255.0f;
        glUniform4f(colorLoc, rcol, g, b, a);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };

    auto drawRect = [&](float x, float y, float w, float h, uint32_t rgba) {
        float verts[8] = { x, y, x+w, y, x, y+h, x+w, y+h };
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
        float a = ((rgba >> 24) & 0xFF) / 255.0f;
        float rcol = ((rgba >> 16) & 0xFF) / 255.0f;
        float g = ((rgba >> 8) & 0xFF) / 255.0f;
        float b = ((rgba) & 0xFF) / 255.0f;
        glUniform4f(colorLoc, rcol, g, b, a);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };

    // ─── Joystick (bottom-left) ──────────────────────────────────────
    auto joy = ctrl.getJoystickState();
    // Outer ring
    drawRing(joy.cx, joy.cy, joy.radius, 8.0f, 0x44FFFFFF);
    // Inner stick
    if (joy.active) {
        float sx = joy.cx + joy.stickX * joy.radius;
        float sy = joy.cy + joy.stickY * joy.radius;
        drawCircle(sx, sy, 45.0f, 0xCCFFFFFF);
    } else {
        drawCircle(joy.cx, joy.cy, 35.0f, 0x66FFFFFF);
    }

    // ─── Action buttons (bottom-right) ────────────────────────────
    TouchController::ButtonState btns[TouchController::MAX_ACTION_BUTTONS];
    int nBtn = ctrl.getActionButtons(btns);
    for (int i = 0; i < nBtn; ++i) {
        const auto& b = btns[i];
        uint32_t col = b.pressed ? (b.color | 0xFF000000) : (b.color & 0xBFFFFFFF);
        drawCircle(b.cx, b.cy, b.radius, col);
        // Power gauge ring (concentric, clean, perfectly aligned around the button)
        if (b.power > 0.0f) {
            float gaugeR = b.radius + 6.0f;
            float thickness = 2.0f + b.power * 4.0f;
            uint32_t gaugeCol = 0xFF000000 | static_cast<uint32_t>(0xFF * b.power) << 16
                                | static_cast<uint32_t>(0xFF * (1.0f - b.power)) << 8;
            drawRing(b.cx, b.cy, gaugeR, thickness, gaugeCol);
        }
        // Inner symbol (simple shapes)
        if (std::strcmp(b.label, "PASS") == 0) {
            // Triangle pointing right
            float s = b.radius * 0.4f;
            float tri[6] = { b.cx - s, b.cy - s, b.cx - s, b.cy + s, b.cx + s*0.7f, b.cy };
            glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STREAM_DRAW);
            glUniform4f(colorLoc, 1,1,1,0.9f);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        } else if (std::strcmp(b.label, "SHOT") == 0) {
            // Circle
            drawCircle(b.cx, b.cy, b.radius * 0.35f, 0xDDFFFFFF);
        } else if (std::strcmp(b.label, "SPRINT") == 0) {
            // Lightning-ish (two lines)
            float s = b.radius * 0.4f;
            drawLine(b.cx - s*0.3f, b.cy - s, b.cx + s*0.2f, b.cy + s*0.1f, 6.0f, 0xDDFFFFFF);
            drawLine(b.cx + s*0.2f, b.cy + s*0.1f, b.cx - s*0.1f, b.cy + s, 6.0f, 0xDDFFFFFF);
        } else if (std::strcmp(b.label, "TACKLE") == 0) {
            // X
            float s = b.radius * 0.35f;
            drawLine(b.cx - s, b.cy - s, b.cx + s, b.cy + s, 5.0f, 0xDDFFFFFF);
            drawLine(b.cx + s, b.cy - s, b.cx - s, b.cy + s, 5.0f, 0xDDFFFFFF);
        }
    }

    // ─── Radar / Minimap (bottom-center) ────────────────────────────
    float radarW = 220.0f, radarH = 140.0f;
    float rx = screenW * 0.5f - radarW * 0.5f;
    float ry = screenH - 220.0f;
    // Background
    drawRect(rx, ry, radarW, radarH, 0xE6004400);
    // Border
    drawRing(rx + radarW*0.5f, ry + radarH*0.5f, std::max(radarW, radarH)*0.5f, 2.0f, 0xAAFFFFFF);
    // Center line
    drawLine(rx + radarW*0.5f, ry, rx + radarW*0.5f, ry + radarH, 1.0f, 0x44FFFFFF);

    // Radar dots
    TouchController::RadarDot dots[TouchController::MAX_RADAR_DOTS];
    int nDots = ctrl.getRadarDots(dots);
    for (int i = 0; i < nDots; ++i) {
        const auto& d = dots[i];
        // Map normalized pitch [-1,1] to radar rect
        float px = rx + (d.nx * 0.5f + 0.5f) * radarW;
        float py = ry + (-d.ny * 0.5f + 0.5f) * radarH; // flip Y
        float r = d.isBall ? 5.0f : 3.5f;
        drawCircle(px, py, r, d.color | 0xFF000000);
        if (d.isBall) {
            drawRing(px, py, 8.0f, 1.5f, 0xFFFFFFFF);
        }
    }

    // ─── Aim indicator (3D world → screen) ─────────────────────────
    auto aim = ctrl.getAimIndicator();
    if (aim.visible && playerPositions && playerFlags) {
        // Find controlled player (flags bit 2)
        int activeIdx = -1;
        for (int i = 0; i < 22; ++i) {
            if (playerFlags[i] & 4) { activeIdx = i; break; }
        }
        if (activeIdx >= 0) {
            // Convert GF env coords to world coords (same as renderPlayers)
            constexpr float kEnvYHalf = 0.4306f;
            float scaleX  = pitchHalf_[0] * 0.1f;
            float scaleZ  = pitchHalf_[1] * 0.1f / kEnvYHalf;
            float pitchScale = pitchHalf_[0] / 52.5f;
            float gx = clampFloat(playerPositions[activeIdx * 3 + 0], -1.05f, 1.05f);
            float gw = clampFloat(playerPositions[activeIdx * 3 + 1], -0.50f, 0.50f);
            float gh = playerPositions[activeIdx * 3 + 2];
            float wx = gx * scaleX;
            float wy = gh * 0.1f * pitchScale + 0.14f * pitchScale;
            float wz = gw * scaleZ;
            // Project world position to screen
            float clip[4];
            for (int j = 0; j < 4; ++j) {
                clip[j] = viewProj[j*4+0]*wx + viewProj[j*4+1]*wy + viewProj[j*4+2]*wz + viewProj[j*4+3];
            }
            if (std::fabs(clip[3]) > 0.001f) {
                float ndcX = clip[0] / clip[3];
                float ndcY = clip[1] / clip[3];
                float scrX = (ndcX * 0.5f + 0.5f) * screenW;
                float scrY = (-ndcY * 0.5f + 0.5f) * screenH;
                // Draw arrow from player toward aim direction
                float len = 80.0f * aim.power;
                float endX = scrX + aim.dirX * len;
                float endY = scrY - aim.dirY * len; // screen Y down
                float a = ((aim.color >> 24) & 0xFF) / 255.0f;
                float rcol = ((aim.color >> 16) & 0xFF) / 255.0f;
                float g = ((aim.color >> 8) & 0xFF) / 255.0f;
                float b = ((aim.color) & 0xFF) / 255.0f;
                drawLine(scrX, scrY, endX, endY, 6.0f, aim.color | 0xFF000000);
                // Arrow head
                float hx = endX, hy = endY;
                float backX = scrX + aim.dirX * (len * 0.7f);
                float backY = scrY - aim.dirY * (len * 0.7f);
                float perpX = -(endY - backY) * 0.3f;
                float perpY = (endX - backX) * 0.3f;
                float headVerts[6] = {
                    hx + perpX, hy + perpY,
                    hx - perpX, hy - perpY,
                    endX + aim.dirX * 15.0f, endY - aim.dirY * 15.0f
                };
                glBufferData(GL_ARRAY_BUFFER, sizeof(headVerts), headVerts, GL_STREAM_DRAW);
                glUniform4f(colorLoc, rcol, g, b, a);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
        }
    }

    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
