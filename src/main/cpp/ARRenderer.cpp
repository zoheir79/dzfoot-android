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

#define LOG_TAG "ARRenderer"
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
            LOGI("  Node[%zu] name='%s' bone=%d meshes=%zu", i, rn.name.c_str(), rn.boneIndex, rn.staticMeshes.size());
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
    if (skinTex)  { glDeleteTextures(1, &skinTex);  skinTex = 0; }
    if (kitTex)   { glDeleteTextures(1, &kitTex);   kitTex = 0; }
    if (shoeTex)  { glDeleteTextures(1, &shoeTex);  shoeTex = 0; }
    if (shortTex && shortTex != kitTex) { glDeleteTextures(1, &shortTex); }
    shortTex = 0;
    defaultSkinTex = 0;
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
                     int playerIndex, const DirAnimClip* dirClip) {
    if (nodes.empty()) return;

    (void)skinnedShader;

    // 1. Per-node animated TRS: start from bind pose, then override animated channels
    const size_t N = nodes.size();
    std::vector<float> nodeT(N * 3), nodeR(N * 4), nodeS(N * 3);
    for (size_t i = 0; i < N; ++i) {
        std::memcpy(&nodeT[i * 3], nodes[i].bindT, 3 * sizeof(float));
        std::memcpy(&nodeR[i * 4], nodes[i].bindR, 4 * sizeof(float));
        std::memcpy(&nodeS[i * 3], nodes[i].bindS, 3 * sizeof(float));
    }

    static int animDbgCounter = 0;
    bool animDbg = (playerIndex == 0) && (animDbgCounter++ % 120 == 0);

    std::vector<float> curT = nodeT, curR = nodeR, curS = nodeS;
    std::vector<float> prevT = nodeT, prevR = nodeR, prevS = nodeS;

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

        if (animDbg) {
            size_t s0in = clip.samplers.empty() ? 0 : clip.samplers[0].input.size();
            size_t s0out = clip.samplers.empty() ? 0 : clip.samplers[0].output.size();
            LOGI("[animDbg] anims=%zu clip=%zu '%s' dur=%.2f tt=%.2f chans=%zu samps=%zu s0in=%zu s0out=%zu",
                 animations.size(), clipIdx, clip.name.c_str(), dur, tt,
                 clip.channels.size(), clip.samplers.size(), s0in, s0out);
        }

        for (const auto& ch : clip.channels) {
            if (ch.targetNode < 0 || ch.targetNode >= (int)N) continue;
            if (ch.sampler < 0 || ch.sampler >= (int)clip.samplers.size()) continue;
            const GLBAnimSampler& s = clip.samplers[ch.sampler];
            float val[4] = { 0, 0, 0, 1 };
            sampleSampler(s, tt, val);
            if (ch.path == 0) {        // translation
                if (ch.targetNode == 0) continue;
                std::memcpy(&curT[ch.targetNode * 3], val, 3 * sizeof(float));
            } else if (ch.path == 1) { // rotation
                std::memcpy(&curR[ch.targetNode * 4], val, 4 * sizeof(float));
                if (animDbg && ch.targetNode == 1) {
                    LOGI("[animDbg] node1 body rot = (%.3f, %.3f, %.3f, %.3f)",
                         val[0], val[1], val[2], val[3]);
                }
            } else if (ch.path == 2) { // scale
                std::memcpy(&curS[ch.targetNode * 3], val, 3 * sizeof(float));
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
                std::memcpy(&prevT[ch.targetNode * 3], val, 3 * sizeof(float));
            } else if (ch.path == 1) {
                std::memcpy(&prevR[ch.targetNode * 4], val, 4 * sizeof(float));
            } else if (ch.path == 2) {
                std::memcpy(&prevS[ch.targetNode * 3], val, 3 * sizeof(float));
            }
        }
    }

    // Blend current and previous TRS poses
    for (size_t i = 0; i < N; ++i) {
        if (isBlending) {
            nodeT[i * 3 + 0] = prevT[i * 3 + 0] + blend * (curT[i * 3 + 0] - prevT[i * 3 + 0]);
            nodeT[i * 3 + 1] = prevT[i * 3 + 1] + blend * (curT[i * 3 + 1] - prevT[i * 3 + 1]);
            nodeT[i * 3 + 2] = prevT[i * 3 + 2] + blend * (curT[i * 3 + 2] - prevT[i * 3 + 2]);

            quatSlerpLocal(&prevR[i * 4], &curR[i * 4], blend, &nodeR[i * 4]);

            nodeS[i * 3 + 0] = prevS[i * 3 + 0] + blend * (curS[i * 3 + 0] - prevS[i * 3 + 0]);
            nodeS[i * 3 + 1] = prevS[i * 3 + 1] + blend * (curS[i * 3 + 1] - prevS[i * 3 + 1]);
            nodeS[i * 3 + 2] = prevS[i * 3 + 2] + blend * (curS[i * 3 + 2] - prevS[i * 3 + 2]);
        } else {
            std::memcpy(&nodeT[i * 3], &curT[i * 3], 3 * sizeof(float));
            std::memcpy(&nodeR[i * 4], &curR[i * 4], 4 * sizeof(float));
            std::memcpy(&nodeS[i * 3], &curS[i * 3], 3 * sizeof(float));
        }
    }

    // 2. Compose local matrices from animated TRS and accumulate into global matrices
    std::vector<float> globalMats(N * 16);
    for (size_t i = 0; i < N; ++i) {
        const auto& rn = nodes[i];
        float localMat[16];
        composeTRS(&nodeT[i * 3], &nodeR[i * 4], &nodeS[i * 3], localMat);

        float* gm = &globalMats[i * 16];
        if (rn.parentIndex >= 0) {
            float* parentGm = &globalMats[rn.parentIndex * 16];
            Transform::mat4Mul(parentGm, localMat, gm);
        } else {
            std::memcpy(gm, localMat, 16 * sizeof(float));
        }
    }
    if (animDbg) {
        LOGI("[animDbg] node0 T=(%.3f,%.3f,%.3f) R=(%.4f,%.4f,%.4f,%.4f) S=(%.3f,%.3f,%.3f)",
             nodeT[0], nodeT[1], nodeT[2], nodeR[0], nodeR[1], nodeR[2], nodeR[3],
             nodeS[0], nodeS[1], nodeS[2]);
        LOGI("[animDbg] node0 globalMat col0=(%.4f,%.4f,%.4f) col3=(%.4f,%.4f,%.4f)",
             globalMats[0], globalMats[1], globalMats[2],
             globalMats[12], globalMats[13], globalMats[14]);
        LOGI("[animDbg] node1 parent=%d T=(%.3f,%.3f,%.3f) R=(%.4f,%.4f,%.4f,%.4f) S=(%.3f,%.3f,%.3f)",
             nodes[1].parentIndex,
             nodeT[3], nodeT[4], nodeT[5], nodeR[4], nodeR[5], nodeR[6], nodeR[7],
             nodeS[3], nodeS[4], nodeS[5]);
        LOGI("[animDbg] node1 globalMat col0=(%.4f,%.4f,%.4f) [0]=(%.6f)",
             globalMats[16], globalMats[17], globalMats[18], globalMats[16]);
    }

    // 3. Build player world model matrix (Translation * rotY rotation * scale)
    float modelRot[16];
    // DERIVED (not guessed): position maps sceneX=gf_X, sceneZ=gf_Y, and server
    // rotY=atan2(dir_gf_X, dir_gf_Y). A model facing -Z (glTF standard) rotated by
    // theta has facing (-sin,-cos); matching it to (dir_gf_X,dir_gf_Y) gives
    // theta = rotY + PI. (rotY alone -> backwards, confirming -Z forward model.)
    const float modelYaw = rotY + 3.14159265f;
    float qRot[4] = { 0.0f, std::sin(modelYaw * 0.5f), 0.0f, std::cos(modelYaw * 0.5f) };
    Transform::quatToMat4(qRot, modelRot);

    const float scaleVal = 0.35f;
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
        Transform::mat4Mul(modelRot, &globalMats[i * 16], partWorld);

        if (animDbg && i == 1) {
            LOGI("[draw] P%d node1 partWorld scaleX=%.2f trans=(%.2f,%.2f,%.2f)",
                 playerIndex, partWorld[0], partWorld[12], partWorld[13], partWorld[14]);
        }

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
                boundTex = skinTex;
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
    if (animDbg) {
        LOGI("[draw] P%d total draw calls=%d", playerIndex, drawCount);
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
uniform int u_MaterialType; // 0=default, 1=pitch, 2=goal, 3=ball, 4=stadium
uniform sampler2D u_BaseTexture;
uniform float u_UseTexture; // 0.0 = color only, 1.0 = texture * color
uniform vec2 u_PitchHalf;   // pitch half-extents in local space (meters)

vec3 grassPitch() {
    vec3 baseGreen = vec3(0.18, 0.34, 0.13);
    vec3 darkGreen = vec3(0.14, 0.28, 0.11);
    vec3 white     = vec3(0.78, 0.76, 0.60);

    // Normalize local position into [-1, 1] across the pitch (robust to GLB units)
    vec2 n = v_LocalPos.xz / max(u_PitchHalf, vec2(0.001));

    // Mowing stripes: ~14 bands along the length, soft contrast
    float stripe = step(0.5, fract(n.x * 6.0));
    vec3 grass = mix(baseGreen, darkGreen, stripe * 0.55);
    float checker = step(0.5, fract(n.y * 5.0));
    grass *= mix(0.96, 1.04, checker);

    // Line thickness in normalized units
    float tx = 0.009;
    float tz = 0.013;

    // Outer boundary (just inside the edge)
    float sideX = step(0.96, abs(n.x)) * step(abs(n.x), 0.99);
    float sideZ = step(0.96, abs(n.y)) * step(abs(n.y), 0.99);

    // Halfway line (x = 0)
    float halfway = step(abs(n.x), tx);

    // Center circle (radius ~0.17 of half-length)
    float d = length(vec2(n.x, n.y * (u_PitchHalf.y / u_PitchHalf.x)));
    float circle = step(0.16, d) * step(d, 0.16 + tx);
    // Center spot
    float spot = step(d, 0.018);

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

vec3 stadiumShader() {
    // Draw horizontal tiers of seats based on height (v_LocalPos.y)
    float tier = step(0.5, fract(v_LocalPos.y * 1.5)); // 1.5 tiers per meter
    vec3 seatColor = vec3(0.35, 0.38, 0.42); // Gray concrete stands

    // Divide stands into alternating red and blue seat blocks around the stadium angle
    float angle = atan(v_LocalPos.z, v_LocalPos.x);
    float seatBlock = step(0.3, sin(angle * 24.0)); // 24 distinct sections
    vec3 colorSeats = mix(vec3(0.80, 0.15, 0.15), vec3(0.15, 0.35, 0.80), step(0.0, sin(angle * 8.0)));

    if (v_LocalPos.y > 1.2) {
        return mix(seatColor, colorSeats * (0.8 + 0.2 * tier), step(0.4, fract(v_LocalPos.y * 1.5)) * seatBlock);
    } else {
        return vec3(0.22, 0.24, 0.26); // Asphalt pathways and surroundings
    }
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
    float amb = 0.55;
    float light = diff1 + diff2 + amb;
    
    vec3 baseColor;
    float alpha = 1.0;

    if (u_MaterialType == 1) {
        baseColor = grassPitch();
    } else if (u_MaterialType == 2) {
        vec4 g = goalShader(vec3(light));
        baseColor = g.rgb;
        alpha = g.a;
        outColor = vec4(baseColor, alpha);
        return;
    } else if (u_MaterialType == 3) {
        baseColor = ballPattern();
    } else if (u_MaterialType == 4) {
        baseColor = stadiumShader();
    } else if (u_UseTexture > 0.5) {
        // Player texture: sample detail texture and tint with team color
        vec3 texColor = texture(u_BaseTexture, v_TexCoord).rgb;
        baseColor = texColor * u_Color;
    } else {
        baseColor = u_Color;
    }

    // Specular highlight for players to show 3D shape and limb definition
    float spec = 0.0;
    if (u_MaterialType == 0) {
        vec3 viewDir = normalize(-v_LocalPos);
        vec3 halfVec = normalize(L1 + viewDir);
        spec = pow(max(dot(N, halfVec), 0.0), 32.0) * 0.5;
    }

    outColor = vec4(baseColor * light + vec3(spec), alpha);
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

static uint8_t sanitizePlayerAnim(uint8_t animId, float speed) {
    // Only trust locomotion anims from server. Other anims (shoot, fall, etc.)
    // are often incorrectly sent or don't match the GLB embedded clips.
    // Server sends DRIBBLE (10) for all moving players; map to RUN for field players.
    // GK_IDLE (13) is kept for goalkeepers.
    if (animId == dzfoot::ANIM_DRIBBLE) return dzfoot::ANIM_RUN;
    bool isServerLocomotion = (animId == dzfoot::ANIM_IDLE ||
                               animId == dzfoot::ANIM_WALK ||
                               animId == dzfoot::ANIM_RUN ||
                               animId == dzfoot::ANIM_SPRINT ||
                               animId == dzfoot::ANIM_GK_IDLE);
    if (isServerLocomotion) return animId;

    uint8_t locomotionAnim = dzfoot::ANIM_IDLE;
    if (speed > 0.008f) locomotionAnim = dzfoot::ANIM_RUN;
    else if (speed > 0.0005f) locomotionAnim = dzfoot::ANIM_WALK;
    return locomotionAnim;
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
    cameraShader_ = Shader::compile(CAMERA_VERT, CAMERA_FRAG);
    skinnedShader_ = Shader::compile(SKINNED_VERT, SKINNED_FRAG);
    staticShader_ = Shader::compile(STATIC_VERT, STATIC_FRAG);
    pitchTex_ = 0;
    ballTex_ = loadAssetTexture("beta2/media/objects/balls/ball.jpg");
    stadiumTex_ = loadAssetTexture("beta2/media/textures/stadium/greenish_floor.png");
    crowdTex_ = loadAssetTexture("beta2/media/textures/stadium/crowd01.png");
    goalnettingTex_ = loadAssetTexture("beta2/media/textures/stadium/goalnetting.png");
    pitchOverlayTex_ = loadAssetTexture("beta2/media/textures/pitch/overlay.png");
    LOGI("Textures: ball=%u stadium=%u crowd=%u goalnetting=%u pitchOverlay=%u",
         ballTex_, stadiumTex_, crowdTex_, goalnettingTex_, pitchOverlayTex_);

    // Load compiled directional animations from asset folder
    dirAnimBank_.load(gAssetManager, "directional_anims.bin");

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

    // 5. Player Base rigid rig
    if (!playerRig_.load("player_base.glb")) {
        LOGE("Could not load player_base.glb rig!");
    }

    scene_.update();
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
    playerRig_.destroy();
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
                               const uint8_t* playerFlags, const uint8_t* playerTeams) {
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
            // Map GF env coords to 3D scene units using pitch model bbox scale
            constexpr float kEnvYHalf = 0.4306f;
            const float halfLen = pitchHalf_[0] * 0.1f;
            const float scaleX  = halfLen;
            const float scaleZ  = pitchHalf_[1] * 0.1f / kEnvYHalf;
            const float bx = clampFloat(ballPosition[0], -1.05f, 1.05f);
            const float bw = clampFloat(ballPosition[1], -0.50f, 0.50f);
            scene_.nodes[ballIdx].local.position[0] = bx * scaleX;
            scene_.nodes[ballIdx].local.position[1] = ballPosition[2] * 0.1f + 0.08f; // height
            scene_.nodes[ballIdx].local.position[2] = bw * scaleZ;                    // width
        }
        
        scene_.update();
    }

    renderStaticObjects(vpa);
    renderPlayers(vpa, playerPositions, numPlayers, playerAnims, playerVels, playerRotY, playerFlags, playerTeams);
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
    glUniform2f(pitchHalfLoc, pitchHalf_[0], pitchHalf_[1]);
    glUniform1f(useTexLoc, 0.0f);

    for (auto& node : scene_.nodes) {
        if (node.useSkinning || !node.visible) continue;
        if (!node.staticMesh.hasData()) continue;

        float mvp[16];
        mat4Mul(viewProj, node.worldMatrix, mvp);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

        int materialType = 0;
        GLuint boundTex = 0;
        if (node.name == "pitch") {
            glUniform3f(colLoc, 0.15f, 0.45f, 0.15f);
            materialType = 1; // procedural grass + lines
        } else if (node.name.find("goal") == 0) {
            glUniform3f(colLoc, 0.95f, 0.95f, 0.95f);
            materialType = 2;
        } else if (node.name == "ball") {
            glUniform3f(colLoc, 1.0f, 1.0f, 1.0f);
            materialType = ballTex_ ? 0 : 3; // procedural football pattern
            boundTex = ballTex_;
        } else if (node.name == "stadium") {
            glUniform3f(colLoc, 0.35f, 0.37f, 0.40f);
            materialType = stadiumTex_ ? 0 : 4; // procedural stadium
            boundTex = stadiumTex_;
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

        node.staticMesh.draw();
    }

    glDisable(GL_BLEND);
}

void ARRenderer::renderPlayers(const float* viewProj, const float* playerPositions, int numPlayers,
                               const uint8_t* playerAnims, const float* playerVels,
                               const float* playerRotY,
                               const uint8_t* playerFlags, const uint8_t* playerTeams) {
    if (playerRig_.nodes.empty()) {
        // fallback: draw tiny cubes if rig not loaded
        static int fallbackCounter = 0;
        bool shouldLog = (fallbackCounter++ % 120 == 0);
        if (shouldLog) LOGI("[ARRenderer::renderPlayers] PlayerRig not loaded, using cube fallback");

        Shader::use(staticShader_);
        GLint mvpLoc = glGetUniformLocation(staticShader_, "u_ModelViewProj");
        GLint colLoc = glGetUniformLocation(staticShader_, "u_Color");
        // Map GF env coords to 3D scene units using pitch model bbox scale
        constexpr float kEnvYHalf = 0.4306f;
        const float halfLen = pitchHalf_[0] * 0.1f;
        const float scaleX  = halfLen;
        const float scaleZ  = pitchHalf_[1] * 0.1f / kEnvYHalf;
        for (int i = 0; i < numPlayers; ++i) {
            // Skip non-active players (sent off / substituted)
            uint8_t flags = playerFlags ? playerFlags[i] : 0xFF;
            if (!(flags & 1)) continue;
            float gx = clampFloat(playerPositions[i * 3 + 0], -1.05f, 1.05f);
            float gw = clampFloat(playerPositions[i * 3 + 1], -0.50f, 0.50f);
            float gh = playerPositions[i * 3 + 2];
            float localModel[16] = {
                0.15f, 0, 0, 0,
                0, 0.15f, 0, 0,
                0, 0, 0.15f, 0,
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
        LOGI("[renderPlayers] Rig nodes=%zu, numPlayers=%d", playerRig_.nodes.size(), numPlayers);
    }

    // GF env_coord ranges: X in [-1,1] (length), Y in [-0.43,0.43] (width)
    // Map to 3D scene units using pitch model bbox scale
    constexpr float kEnvYHalf = 0.4306f;
    const float halfLen = pitchHalf_[0] * 0.1f;
    const float scaleX  = halfLen;
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

        // Heading: prefer the server-computed rotY (derived from the player's
        // internal direction). Fall back to velocity-derived heading (length,width)
        // only if rotY is unavailable.
        float vx = playerVels ? playerVels[i * 3 + 0] : 0.0f;
        float vz = playerVels ? playerVels[i * 3 + 1] : 1.0f;
        // Server rotY = atan2(dirX, dirZ) in GF coords. Fallback must match exactly.
        float rotY = playerRotY ? playerRotY[i] : std::atan2(vx, vz);

        // Animation state for this player
        uint8_t rawAnim = playerAnims ? playerAnims[i] : 0;
        uint8_t desiredAnim = rawAnim;
        float speed = std::sqrt(vx * vx + vz * vz);
        desiredAnim = sanitizePlayerAnim(desiredAnim, speed);
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
            // moveAngle must be in same space as server rotY (atan2(dirX, dirZ))
            float moveAngle = std::atan2(vx, vz);
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
        bool teamA = playerTeams ? (playerTeams[i] == 0) : (i < 11);
        // Realistic team kit colors: Team A = deep blue, Team B = deep red
        float teamColor[3] = { teamA ? 0.05f : 0.80f, teamA ? 0.15f : 0.05f, teamA ? 0.70f : 0.05f };

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

        if (shouldLog) {
            LOGI("[renderPlayers] P%d pos=(%.2f,%.2f,%.2f) rawAnim=%d anim=%d relAngle=%.1f clip=%s blend=%.2f time=%.2f vel=(%.2f,%.2f) rotY=%.2f team=%d",
                 i, gx, gw, gh,
                 rawAnim, playerAnims_[i].current, relAngleDeg,
                 dirClip ? "DIR_CLIP" : "GLB_FALLBACK",
                 playerAnims_[i].blend, playerAnims_[i].time, vx, vz, rotY,
                 (int)(playerTeams ? playerTeams[i] : (i < 11 ? 0 : 1)));
        }

        playerRig_.draw(viewProj, worldPos, rotY,
                        playerAnims_[i].current, playerAnims_[i].previous, playerAnims_[i].blend,
                        playerAnims_[i].time, playerAnims_[i].prevTime,
                        staticShader_, skinnedShader_, teamColor, i, dirClip);
    }
}
 
