#include "ARRenderer.h"
#include "Shader.h"
#include "Mesh.h"
#include "GLBLoader.h"
#include "AssetLoader.h"
#include "AnimationPlayer.h"
#include "protocol/DZFootProtocol.h"
#include <android/log.h>
#include <jni.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <chrono>

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
        for (const auto& prim : mesh.primitives) {
            if (prim.vertices.empty()) continue;
            SkinnedMesh m;
            m.upload(prim.vertices, prim.indices);
            MeshPart mp;
            mp.mesh = std::move(m);
            mp.materialIndex = prim.materialIndex;
            mp.materialName = materialCat; // override with client's category
            // Copy base color if material exists
            if (prim.materialIndex >= 0 && prim.materialIndex < (int)scene.materials.size()) {
                std::memcpy(mp.baseColor, scene.materials[prim.materialIndex].baseColor, 4 * sizeof(float));
            }
            attachedMeshes.push_back(std::move(mp));
            LOGI("PlayerRig: attached part %s mesh to '%s' (verts=%zu)",
                 partGlb, parentBoneName, prim.vertices.size());
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

    // 2. Apply height scale on root node
    int rootIdx = findNodeIndex("player");
    if (rootIdx >= 0) {
        nodes[rootIdx].bindS[1] = cfg.height;
        // Also update the node's scale for immediate visual effect
        float* s = nodes[rootIdx].bindS;
        // If node already has a scale from GLB, multiply
        // For now just set Y scale
        // (The draw loop uses bindS to compose matrices)
    }

    // 3. Attach hair (if not bald)
    if (cfg.hairStyle != 5) { // 5 = bald
        const char* hair_names[6] = {"short", "long", "mohawk", "curly", "ponytail", "bald"};
        const char* hs = hair_names[cfg.hairStyle % 6];
        char hairPath[128];
        snprintf(hairPath, sizeof(hairPath), "modular/parts/hair_%s.glb", hs);
        attachPart(hairPath, "head", "hair");
    }

    // 4. Attach beard (if not none)
    if (cfg.beardStyle != 0) { // 0 = none
        const char* beard_names[4] = {"none", "stubble", "short", "full"};
        const char* bs = beard_names[cfg.beardStyle % 4];
        char beardPath[128];
        snprintf(beardPath, sizeof(beardPath), "modular/parts/beard_%s.glb", bs);
        attachPart(beardPath, "head", "beard");
    }

    LOGI("PlayerRig: modular avatar loaded (body=%s hair=%d beard=%d skin=%d height=%.2f)",
         bt, cfg.hairStyle, cfg.beardStyle, cfg.skinColor, cfg.height);
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
                     int playerIndex, const DirAnimClip* dirClip,
                     const AvatarConfig* avatar) {
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

    const float scaleVal = 0.10f; // consistent with environment scale (pitch/goals/ball/stadium all use 0.1)
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
    static GLint colLoc = -1;
    static GLint texLoc = -1;
    static GLint useTexLoc = -1;
    static GLuint lastShader = 0;

    if (shader != lastShader) {
        mvpLoc = glGetUniformLocation(shader, "u_ModelViewProj");
        colLoc = glGetUniformLocation(shader, "u_Color");
        texLoc = glGetUniformLocation(shader, "u_BaseTexture");
        useTexLoc = glGetUniformLocation(shader, "u_UseTexture");
        lastShader = shader;
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

        for (const auto& part : rn.staticMeshes) {
            float partColor[3];
            partColor[0] = part.baseColor[0];
            partColor[1] = part.baseColor[1];
            partColor[2] = part.baseColor[2];

            GLuint boundTex = 0;
            if (part.materialName == "skin") {
                if (avatar && avatar->skinColor < 7 && skinTexs[avatar->skinColor]) {
                    boundTex = skinTexs[avatar->skinColor];
                } else {
                    boundTex = skinTex;
                }
            } else if (part.materialName == "head_skin") {
                if (avatar && avatar->skinColor < 7 && skinTexs[avatar->skinColor]) {
                    boundTex = skinTexs[avatar->skinColor];
                } else {
                    boundTex = skinTex;
                }
            } else if (part.materialName == "hair") {
                if (avatar && avatar->hairColor < 8 && hairTexs[avatar->hairColor]) {
                    boundTex = hairTexs[avatar->hairColor];
                } else {
                    boundTex = kitTex; // fallback
                }
            } else if (part.materialName == "beard") {
                if (avatar && avatar->hairColor < 8 && hairTexs[avatar->hairColor]) {
                    boundTex = hairTexs[avatar->hairColor];
                } else {
                    boundTex = kitTex; // fallback
                }
            } else if (part.materialName == "kit_upper") {
                boundTex = kitTex;
                partColor[0] *= teamColor[0];
                partColor[1] *= teamColor[1];
                partColor[2] *= teamColor[2];
            } else if (part.materialName == "kit_lower") {
                boundTex = shortTex;
                partColor[0] *= teamColor[0];
                partColor[1] *= teamColor[1];
                partColor[2] *= teamColor[2];
            } else if (part.materialName == "shoe") {
                boundTex = shoeTex;
            }

            if (boundTex && texLoc >= 0 && useTexLoc >= 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, boundTex);
                glUniform1i(texLoc, 0);
                glUniform1f(useTexLoc, 1.0f);
            } else if (useTexLoc >= 0) {
                glUniform1f(useTexLoc, 0.0f);
            }

            glUniform3fv(colLoc, 1, partColor);
            part.mesh.draw();
            drawCount++;
        }
    }

    // 6. Draw attached modular meshes (hair/beard) using the head bone's transform
    if (!attachedMeshes.empty()) {
        int headIdx = findNodeIndex("head");
        if (headIdx >= 0) {
            float headWorld[16];
            Transform::mat4Mul(modelRot, &scratchGlobalMats[headIdx * 16], headWorld);
            float headMvp[16];
            Transform::mat4Mul(viewProj, headWorld, headMvp);
            glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, headMvp);
            for (const auto& part : attachedMeshes) {
                float partColor[3] = {part.baseColor[0], part.baseColor[1], part.baseColor[2]};
                GLuint boundTex = 0;
                if (part.materialName == "hair") {
                    if (avatar && avatar->hairColor < 8 && hairTexs[avatar->hairColor]) {
                        boundTex = hairTexs[avatar->hairColor];
                    }
                } else if (part.materialName == "beard") {
                    if (avatar && avatar->hairColor < 8 && hairTexs[avatar->hairColor]) {
                        boundTex = hairTexs[avatar->hairColor];
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
                glUniform3fv(colLoc, 1, partColor);
                part.mesh.draw();
                drawCount++;
            }
        }
    }
    // draw logging removed for performance
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

void main() {
    vec3 N = normalize(v_Normal);
    vec3 V = normalize(-v_WorldPos);

    // Dominant sun light (south-west, high angle) — mimics PC's SetRandomSunParams()
    vec3 sunDir = normalize(vec3(0.35, 0.85, 0.45));
    float sunDiff = max(dot(N, sunDir), 0.0);
    
    // Cool fill from opposite side (sky bounce)
    vec3 fillDir = normalize(vec3(-0.4, 0.3, -0.2));
    float fillDiff = max(dot(N, fillDir), 0.0) * 0.25;
    
    // Stadium spotlights (4 corners) — warm white floodlights
    vec3 spotDirs[4] = vec3[](
        normalize(vec3( 30.0, 20.0,  20.0)),
        normalize(vec3(-30.0, 20.0,  20.0)),
        normalize(vec3( 30.0, 20.0, -20.0)),
        normalize(vec3(-30.0, 20.0, -20.0))
    );
    float spotDiff = 0.0;
    for (int i = 0; i < 4; i++) {
        spotDiff += max(dot(N, spotDirs[i]), 0.0) * 0.25;
    }

    // Perimeter advertising-board LED flood (panels along sidelines)
    float ledDiff = max(dot(N, normalize(vec3( 0.8, 0.1, 0.6))), 0.0) * 0.15
                  + max(dot(N, normalize(vec3(-0.8, 0.1, 0.6))), 0.0) * 0.15;

    float amb = 0.28;
    // Sun is the main light (1.25x intensity), fill + spots complement
    float light = sunDiff * 1.25 + fillDiff + spotDiff * 0.45 + ledDiff + amb;

    // Specular highlight for kit (sun direction)
    vec3 R = reflect(-sunDir, N);
    float spec = pow(max(dot(V, R), 0.0), 16.0) * 0.3;

    // Rim light for player edge definition
    float rim = 1.0 - max(dot(N, V), 0.0);
    rim = pow(rim, 3.0) * 0.25;

    vec3 baseColor = u_Color;
    if (u_UseTexture > 0.5) {
        vec3 texColor = texture(u_BaseTexture, v_TexCoord).rgb;
        baseColor = texColor * u_Color;
    }

    // Volumetric spotlight dust glow (warm foggy beams in the stadium)
    float volumeGlow = 0.0;
    vec3 towerPositions[4] = vec3[](
        vec3( 10.0, 3.80,  8.0), // True top-right corner floodlight pylon of stadium_test.glb
        vec3(-10.0, 3.80,  8.0), // True top-left corner floodlight pylon of stadium_test.glb
        vec3( 10.0, 3.80, -8.0), // True bottom-right corner floodlight pylon of stadium_test.glb
        vec3(-10.0, 3.80, -8.0)  // True bottom-left corner floodlight pylon of stadium_test.glb
    );
    for (int i = 0; i < 4; i++) {
        vec3 lightPos = towerPositions[i];
        vec3 toFrag = v_WorldPos - lightPos;
        float dist = length(toFrag);
        vec3 coneDir = normalize(vec3(0.0, -0.4, 0.0) - lightPos);
        float coneAngle = dot(normalize(toFrag), coneDir);
        if (coneAngle > 0.82) {
            float intensity = pow((coneAngle - 0.82) / 0.18, 2.5);
            volumeGlow += (intensity * 0.25) / (1.0 + dist * 0.22);
        }
    }

    vec3 finalColor = baseColor * light + vec3(spec) + vec3(rim) + vec3(volumeGlow) * vec3(1.0, 0.93, 0.82);

    // Warm lens-flare bloom and light overflow on bright spots
    vec3 bloom = max(finalColor - vec3(0.60), vec3(0.0)) * 0.45;
    outColor = vec4(finalColor + bloom, 1.0);
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
out vec3 v_WorldPos;
void main() {
    v_Normal = a_Normal;
    v_TexCoord = a_TexCoord;
    v_LocalPos = a_Position;
    v_WorldPos = a_Position * 0.10f; // Scale local positions to match scaled world space (meters)
    gl_Position = u_ModelViewProj * vec4(a_Position, 1.0);
}
)";

static const char* STATIC_FRAG = R"(#version 300 es
precision mediump float;
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_LocalPos;
in vec3 v_WorldPos;
out vec4 outColor;
uniform vec3 u_Color;
uniform int u_MaterialType; // 0=default, 1=pitch, 2=goal, 3=ball, 4=stadium
uniform sampler2D u_BaseTexture;
uniform sampler2D u_OverlayTexture;
uniform float u_UseTexture; // 0.0 = color only, 1.0 = texture * color
uniform float u_UseOverlay; // 0.0 = no overlay, 1.0 = overlay * base
uniform vec2 u_PitchHalf;   // pitch half-extents in local space (meters)

uniform float u_Time;

vec3 grassPitch() {
    vec3 baseGreen = vec3(0.35, 0.52, 0.26); // warm bright lawn stripe from Beta 2 v0.2
    vec3 darkGreen = vec3(0.28, 0.44, 0.21); // rich mid green lawn stripe from Beta 2 v0.2

    // Normalize local position into [-1, 1] across the pitch (robust to GLB units)
    vec2 n = v_LocalPos.xz / max(u_PitchHalf, vec2(0.001));

    // Mowing stripes: ~12 bands along the length, parallel to center line
    float stripe = step(0.5, fract(n.x * 6.0));
    vec3 grass = mix(baseGreen, darkGreen, stripe);
    
    // Surface noise: multi-frequency sine waves to break uniformity (PC uses Perlin noise)
    // Low freq variation + mid freq detail + high freq micro-detail
    float noise1 = sin(v_LocalPos.x * 23.7 + v_LocalPos.z * 17.3) * 0.018;
    float noise2 = sin(v_LocalPos.x * 47.1 - v_LocalPos.z * 31.5 + 1.3) * 0.009;
    float noise3 = sin(v_LocalPos.x * 89.3 + v_LocalPos.z * 67.7 + 2.7) * 0.004;
    grass += vec3(noise1 + noise2 + noise3);
    
    return grass;
}

vec3 ballPattern() {
    // Robust 3D soccer ball pattern using local positions on the sphere (independent of UVs)
    float pattern = step(0.15, sin(v_LocalPos.x * 12.0) * sin(v_LocalPos.y * 12.0) * sin(v_LocalPos.z * 12.0));
    return mix(vec3(0.98, 0.98, 0.98), vec3(0.08, 0.08, 0.08), pattern);
}

vec3 stadiumShader() {
    // Height Y boundaries from stadium_test.glb:
    //  - Y > 15.0: Tower-top floodlights (blazing emissive glow)
    //  - 10.0 < Y <= 15.0: Roof structures and supporting trusses
    //  - 6.6 < Y <= 10.0: Upper promenade and walkways
    //  - 1.0 < Y <= 6.6: Seating stands (tiers of colored blocks)
    //  - Y <= 1.0: Pitch apron and advertising boards

    if (v_LocalPos.y > 15.0) {
        // High intensity warm white floodlight glow (overflows for bloom)
        return vec3(2.5, 2.3, 1.8);
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
        // Seating stands: alternate blocks of red and green (national colors)
        float seatRow = step(0.5, fract(v_LocalPos.y * 2.4));
        float angle = atan(v_LocalPos.z, v_LocalPos.x);
        float seatBlock = step(0.3, sin(angle * 32.0)); // 32 spectator blocks
        
        // Classic red-green fan sections
        vec3 colorSeats = mix(vec3(0.72, 0.10, 0.10), vec3(0.10, 0.52, 0.15), step(0.0, sin(angle * 12.0)));
        vec3 concreteStairs = vec3(0.36, 0.38, 0.42);
        vec3 seats = mix(concreteStairs, colorSeats * (0.82 + 0.18 * seatRow), step(0.45, fract(v_LocalPos.y * 2.4)) * seatBlock);
        
        // Blend crowd texture when available (crowd01.png mapped radially on stands)
        if (u_UseTexture > 0.5) {
            vec2 crowdUV = vec2(fract(angle * 0.15 + 0.5), fract(v_LocalPos.y * 0.15));
            vec3 crowd = texture(u_BaseTexture, crowdUV).rgb;
            seats = mix(seats, crowd, 0.55);
        }
        return seats;
    } 
    
    // Pitch apron and advertising panels (Y <= 1.0)
    if (v_LocalPos.y > 0.15) {
        float x = v_LocalPos.x;
        float y = v_LocalPos.y;
        float t = u_Time * 1.5; // scrolling speed
        
        // Split into digital screen blocks
        float panelIndex = floor(x * 0.1);
        float px = fract(x * 0.1);    // [0, 1] horizontally on panel
        float py = (y - 0.15) / 0.85; // [0, 1] vertically on panel
        
        // High-quality LED dot-matrix grid effect
        float ledGrid = step(0.15, fract(px * 100.0)) * step(0.15, fract(py * 16.0));
        
        vec3 ledColor = vec3(0.05); // background / off-state
        
        // Rotate through 3 different animated advertising boards
        int adStyle = int(mod(panelIndex + floor(u_Time * 0.15), 3.0));
        
        if (adStyle == 0) {
            // Gold/Orange JIGSAW HD / PDTV brand style with sliding glowing waves
            float letters = sin(px * 12.0 + t) * sin(py * 3.14159);
            vec3 activeColor = mix(vec3(0.9, 0.5, 0.1), vec3(0.95, 0.8, 0.1), step(0.0, letters));
            ledColor = mix(ledColor, activeColor, step(0.1, letters) * ledGrid);
        } else if (adStyle == 1) {
            // Scrolling neon-blue chevron indicator boards
            float chevron = step(0.4, fract(px * 8.0 - t));
            ledColor = mix(ledColor, vec3(0.1, 0.7, 0.95), chevron * ledGrid);
        } else {
            // Mobilis / JSK Algeria green and white stripes
            float stripes = step(0.5, fract(px * 6.0 + py * 2.0 - t * 0.5));
            vec3 brandColor = mix(vec3(0.05, 0.55, 0.15), vec3(0.9, 0.9, 0.9), stripes);
            ledColor = mix(ledColor, brandColor, ledGrid);
        }
        
        // Emissive boost so panels glow and bloom beautifully in the stadium
        return ledColor * 2.2;
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

void main() {
    vec3 N = normalize(v_Normal);
    vec3 L1 = normalize(vec3(0.3, 1.0, 0.4));
    vec3 L2 = normalize(vec3(-0.5, 0.5, -0.3));
    float diff1 = max(dot(N, L1), 0.0);
    float diff2 = max(dot(N, L2), 0.0) * 0.3;

    // Stadium spotlights (4 corners) — warm white floodlights
    vec3 spotDirs[4] = vec3[](
        normalize(vec3( 30.0, 20.0,  20.0)),
        normalize(vec3(-30.0, 20.0,  20.0)),
        normalize(vec3( 30.0, 20.0, -20.0)),
        normalize(vec3(-30.0, 20.0, -20.0))
    );
    float spotDiff = 0.0;
    for (int i = 0; i < 4; i++) {
        spotDiff += max(dot(N, spotDirs[i]), 0.0) * 0.25;
    }

    // Perimeter advertising-board LED flood (panels along sidelines)
    float ledDiff = max(dot(N, normalize(vec3( 0.8, 0.1, 0.6))), 0.0) * 0.05
                  + max(dot(N, normalize(vec3(-0.8, 0.1, 0.6))), 0.0) * 0.05;

    float amb = 0.40;
    float light = diff1 * 0.40 + diff2 * 0.20 + spotDiff * 0.20 + ledDiff + amb;
    light = clamp(light, 0.0, 1.0);

    vec3 baseColor;
    float alpha = 1.0;

    if (u_MaterialType == 1) {
        vec3 grass = grassPitch();
        if (u_UseTexture > 0.5) {
            // High frequency tiling detail for realistic grass (using raw world coordinates for seamless alignment)
            vec3 grassTex = texture(u_BaseTexture, v_LocalPos.xz * 0.6).rgb;
            // Multiply texture details to preserve the sunny hues of the mowing stripes
            grass = mix(grass, grass * grassTex * 1.4, 0.22);
        }
        
        // Projects the stadium roof structural shadows perfectly on the ground!
        if (u_UseOverlay > 0.5) {
            // Compute a single, non-repeating UV coordinate across the entire pitch bounds
            vec2 shadowUV = (v_LocalPos.xz / max(u_PitchHalf, vec2(0.001))) * 0.5 + vec2(0.5);
            vec3 shadow = texture(u_OverlayTexture, shadowUV).rgb;
            // Modulate light: shadow.r = 1.0 (lit area) gets 1.35 (sunny), shadow.r = 0.0 (shadow) gets 0.65 (visible)
            light = mix(0.65, 1.35, shadow.r);
        }
        
        baseColor = grass;
    } else if (u_MaterialType == 2) {
        vec4 g = goalShader(vec3(light));
        baseColor = g.rgb;
        alpha = g.a;
        outColor = vec4(baseColor, alpha);
        return;
    } else if (u_MaterialType == 3) {
        if (u_UseTexture > 0.5) {
            baseColor = texture(u_BaseTexture, v_TexCoord).rgb;
        } else {
            baseColor = ballPattern();
        }
    } else if (u_MaterialType == 4) {
        vec3 base = stadiumShader();
        if (u_UseTexture > 0.5) {
            vec3 texColor = texture(u_BaseTexture, v_TexCoord * 8.0).rgb; // tile for concrete details
            base = mix(base, texColor * vec3(0.9, 0.93, 0.95), 0.25);
        }
        baseColor = base;
    } else if (u_UseTexture > 0.5) {
        vec3 texColor = texture(u_BaseTexture, v_TexCoord).rgb;
        baseColor = texColor * u_Color;
    } else {
        baseColor = u_Color;
    }

    // Specular highlight
    float spec = 0.0;
    if (u_MaterialType == 0) {
        vec3 viewDir = normalize(-v_LocalPos);
        vec3 halfVec = normalize(L1 + viewDir);
        spec = pow(max(dot(N, halfVec), 0.0), 32.0) * 0.5;
    }

    // Volumetric spotlight dust glow (warm foggy beams in the stadium)
    float volumeGlow = 0.0;
    vec3 towerPositions[4] = vec3[](
        vec3( 10.0, 3.80,  8.0), // True top-right corner floodlight pylon of stadium_test.glb
        vec3(-10.0, 3.80,  8.0), // True top-left corner floodlight pylon of stadium_test.glb
        vec3( 10.0, 3.80, -8.0), // True bottom-right corner floodlight pylon of stadium_test.glb
        vec3(-10.0, 3.80, -8.0)  // True bottom-left corner floodlight pylon of stadium_test.glb
    );
    for (int i = 0; i < 4; i++) {
        vec3 lightPos = towerPositions[i];
        vec3 toFrag = v_WorldPos - lightPos;
        float dist = length(toFrag);
        vec3 coneDir = normalize(vec3(0.0, -0.4, 0.0) - lightPos);
        float coneAngle = dot(normalize(toFrag), coneDir);
        if (coneAngle > 0.82) {
            float intensity = pow((coneAngle - 0.82) / 0.18, 2.5);
            volumeGlow += (intensity * 0.25) / (1.0 + dist * 0.22);
        }
    }

    vec3 finalColor = baseColor * light + vec3(spec) + vec3(volumeGlow) * vec3(1.0, 0.93, 0.82);

    // Warm lens-flare bloom and light overflow on bright spots
    vec3 bloom = max(finalColor - vec3(0.60), vec3(0.0)) * 0.45;
    outColor = vec4(finalColor + bloom, alpha);
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
    LOGI("[initTiming] Shaders: %.1f ms", nowMs() - t0);

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
        LOGI("Pitch half-extents (local meters): X=%.2f Z=%.2f", pitchHalf_[0], pitchHalf_[1]);
        // Real pitch GLB is in meters, scale down to fit camera view
        scene_.nodes[pitch].local.scale[0] = 0.1f;
        scene_.nodes[pitch].local.scale[1] = 0.1f;
        scene_.nodes[pitch].local.scale[2] = 0.1f;
        constexpr float kEnvYHalf = 0.4306f; // exact GF pitchHalfH/Y_FIELD_SCALE = 36/83.6
        const float scaleX = pitchHalf_[0] * 0.1f;
        const float scaleZ = pitchHalf_[1] * 0.1f / kEnvYHalf;
        LOGI("Player scales derived from actual pitch GLB: scaleX=%.3f scaleZ=%.3f",
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
        // Ball: use same 0.1 scale as pitch so real-world proportions are preserved.
        // ball.glb is ~22cm diameter; 0.1 keeps it visually correct vs the field.
        scene_.nodes[ball].local.scale[0] = 0.1f;
        scene_.nodes[ball].local.scale[1] = 0.1f;
        scene_.nodes[ball].local.scale[2] = 0.1f;
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

    scene_.update();
    LOGI("[initTiming] scene_.update: %.1f ms", nowMs() - t0);
}

void ARRenderer::destroy() {
    Shader::destroy(cameraShader_);
    Shader::destroy(skinnedShader_);
    Shader::destroy(staticShader_);
    cameraShader_ = 0;
    skinnedShader_ = 0;
    staticShader_ = 0;
    if (quadVbo_) { glDeleteBuffers(1, &quadVbo_); quadVbo_ = 0; }
    if (uvVbo_) { glDeleteBuffers(1, &uvVbo_); uvVbo_ = 0; }
    if (pitchTex_) { glDeleteTextures(1, &pitchTex_); pitchTex_ = 0; }
    if (ballTex_) { glDeleteTextures(1, &ballTex_); ballTex_ = 0; }
    if (stadiumTex_) { glDeleteTextures(1, &stadiumTex_); stadiumTex_ = 0; }
    if (crowdTex_) { glDeleteTextures(1, &crowdTex_); crowdTex_ = 0; }
    if (goalnettingTex_) { glDeleteTextures(1, &goalnettingTex_); goalnettingTex_ = 0; }
    if (pitchOverlayTex_) { glDeleteTextures(1, &pitchOverlayTex_); pitchOverlayTex_ = 0; }
    dirAnimBank_.unload();
    for (int r = 0; r < 22; ++r) playerRigs_[r].destroy();
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
                               const uint8_t* playerRoles) {
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

    renderStaticObjects(vpa);
    renderPlayers(vpa, playerPositions, numPlayers, playerAnims, playerVels, playerRotY, playerFlags, playerTeams, playerRoles);
}

void ARRenderer::renderStaticObjects(const float* viewProj) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    Shader::use(staticShader_);
    GLint mvpLoc = glGetUniformLocation(staticShader_, "u_ModelViewProj");
    GLint colLoc = glGetUniformLocation(staticShader_, "u_Color");
    GLint matLoc = glGetUniformLocation(staticShader_, "u_MaterialType");
    GLint pitchHalfLoc = glGetUniformLocation(staticShader_, "u_PitchHalf");
    GLint useTexLoc = glGetUniformLocation(staticShader_, "u_UseTexture");
    GLint texLoc = glGetUniformLocation(staticShader_, "u_BaseTexture");
    GLint useOverlayLoc = glGetUniformLocation(staticShader_, "u_UseOverlay");
    GLint overlayTexLoc = glGetUniformLocation(staticShader_, "u_OverlayTexture");
    GLint timeLoc = glGetUniformLocation(staticShader_, "u_Time");
    glUniform2f(pitchHalfLoc, pitchHalf_[0], pitchHalf_[1]);
    glUniform1f(useTexLoc, 0.0f);
    glUniform1f(useOverlayLoc, 0.0f);
    if (timeLoc >= 0) {
        glUniform1f(timeLoc, static_cast<float>(nowMs() * 0.001));
    }

    for (auto& node : scene_.nodes) {
        if (node.useSkinning || !node.visible) continue;
        if (!node.staticMesh.hasData()) continue;

        float mvp[16];
        mat4Mul(viewProj, node.worldMatrix, mvp);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

        int materialType = 0;
        GLuint boundTex = 0;
        bool hasOverlay = false;
        if (node.name == "pitch") {
            glUniform3f(colLoc, 1.0f, 1.0f, 1.0f);
            materialType = 1; // pitch: beta2 texture dominant with procedural line overlay
            boundTex = pitchTex_ ? pitchTex_ : pitchOverlayTex_;
            
            // Multitexturing: bind shadow overlay texture to GL_TEXTURE1
            if (pitchOverlayTex_ && useOverlayLoc >= 0 && overlayTexLoc >= 0) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, pitchOverlayTex_);
                glUniform1i(overlayTexLoc, 1);
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
            // Use crowd texture for seating area blend; fallback to stadiumTex for concrete detail
            if (crowdTex_) {
                glUniform3f(colLoc, 1.0f, 1.0f, 1.0f); // white so crowd texture isn't tinted
                boundTex = crowdTex_;
            } else if (stadiumTex_) {
                glUniform3f(colLoc, 1.0f, 1.0f, 1.0f);
                boundTex = stadiumTex_;
            } else {
                glUniform3f(colLoc, 0.35f, 0.37f, 0.40f);
            }
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
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
    }

    glDisable(GL_BLEND);
}

void ARRenderer::renderPlayers(const float* viewProj, const float* playerPositions, int numPlayers,
                               const uint8_t* playerAnims, const float* playerVels,
                               const float* playerRotY,
                               const uint8_t* playerFlags, const uint8_t* playerTeams,
                               const uint8_t* playerRoles) {
    // Check if at least one rig is loaded
    bool anyRigLoaded = false;
    for (int r = 0; r < 22; ++r) {
        if (!playerRigs_[r].nodes.empty()) { anyRigLoaded = true; break; }
    }
    if (!anyRigLoaded) {
        // fallback: draw tiny cubes if no rig loaded
        static int fallbackCounter = 0;
        bool shouldLog = (fallbackCounter++ % 120 == 0);
        if (shouldLog) LOGI("[ARRenderer::renderPlayers] No PlayerRig loaded, using cube fallback");

        Shader::use(staticShader_);
        GLint mvpLoc = glGetUniformLocation(staticShader_, "u_ModelViewProj");
        GLint colLoc = glGetUniformLocation(staticShader_, "u_Color");
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
            bool teamA = playerTeams ? (playerTeams[i] == 0) : (i < 11);
            glUniform3f(colLoc, teamA ? 0.0f : 1.0f, teamA ? 0.5f : 0.0f, teamA ? 1.0f : 0.0f);
        }
        return;
    }

    Shader::use(staticShader_);
    GLint mvpLoc = glGetUniformLocation(staticShader_, "u_ModelViewProj");
    GLint colLoc = glGetUniformLocation(staticShader_, "u_Color");
    GLint matLoc = glGetUniformLocation(staticShader_, "u_MaterialType");
    GLint useTexLoc = glGetUniformLocation(staticShader_, "u_UseTexture");
    glUniform1i(matLoc, 0); // force default color mode so ballPattern is NOT used
    glUniform1f(useTexLoc, 0.0f);

    static int playerFrameCounter = 0;
    bool shouldLog = (playerFrameCounter++ % 120 == 0);
    if (shouldLog) {
        size_t totalNodes = 0;
        for (int r = 0; r < 22; ++r) totalNodes += playerRigs_[r].nodes.size();
        LOGI("[renderPlayers] Total rig nodes=%zu, numPlayers=%d", totalNodes, numPlayers);
    }

    // GF env_coord ranges: X in [-1,1] (length), Y in [-0.43,0.43] (width)
    // Map to 3D scene units derived from the ACTUAL pitch GLB.
    constexpr float kEnvYHalf = 0.4306f; // exact GF pitchHalfH/Y_FIELD_SCALE = 36/83.6
    const float scaleX  = pitchHalf_[0] * 0.1f;
    const float scaleZ  = pitchHalf_[1] * 0.1f / kEnvYHalf;

    for (int i = 0; i < numPlayers; ++i) {
        // --- Exploit server flags ---
        uint8_t flags = playerFlags ? playerFlags[i] : 0xFF;
        // bit 0 = is_active: skip non-active players (sent off / substituted)
        if (!(flags & 1)) continue;

        float gx = clampFloat(playerPositions[i * 3 + 0], -1.05f, 1.05f);
        float gw = clampFloat(playerPositions[i * 3 + 1], -0.50f, 0.50f);
        float gh = playerPositions[i * 3 + 2];
        float worldPos[3] = { gx * scaleX, gh * 0.1f + 0.15f, gw * scaleZ };

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
        cfg.skinColor = 3; cfg.hairColor = 0; cfg.height = 1.0f;
        if (hasSetup_ && i < 22) {
            const auto& p = setup_.players[i];
            cfg.bodyType    = (p.bodyType < 4)    ? p.bodyType    : 1;
            cfg.hairStyle   = (p.hairStyle < 6)   ? p.hairStyle   : 0;
            cfg.beardStyle  = (p.beardStyle < 4)  ? p.beardStyle  : 0;
            cfg.skinColor   = (p.skinColor < 7)   ? p.skinColor   : 3;
            cfg.hairColor   = (p.hairColor < 8)   ? p.hairColor   : 0;
            cfg.height      = (p.height > 0.5f && p.height < 2.5f) ? p.height : 1.0f;
        }

        // Lazy-load modular avatar per player index
        if (playerRigs_[i].nodes.empty()) {
            if (!playerRigs_[i].loadModular(cfg)) {
                // Fallback: copy from rig 0 if available
                if (!playerRigs_[0].nodes.empty()) {
                    // Can't easily copy PlayerRig (contains GPU resources), skip for now
                    LOGI("[renderPlayers] Player %d modular load failed, will use fallback", i);
                }
            }
        }

        // Use player's own rig if loaded, otherwise fall back to rig 0
        PlayerRig* rig = &playerRigs_[i];
        if (rig->nodes.empty()) rig = &playerRigs_[0];

        rig->draw(viewProj, worldPos, rotY,
                  playerAnims_[i].current, playerAnims_[i].previous, playerAnims_[i].blend,
                  playerAnims_[i].time, playerAnims_[i].prevTime,
                  staticShader_, skinnedShader_, teamColor, i, dirClip, &cfg);
    }
}
 
