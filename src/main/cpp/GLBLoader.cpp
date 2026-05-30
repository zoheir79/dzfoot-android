#include "GLBLoader.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <android/log.h>

#define LOG_TAG "GLBLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static bool readUint32LE(const uint8_t* p, uint32_t& out) {
    out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return true;
}

static float readFloatLE(const uint8_t* p) {
    union { uint32_t u; float f; } v;
    v.u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return v.f;
}

// Minimal JSON tokenizer for GLTF embedded JSON
static const char* jsonFindKey(const char* json, size_t len, const char* key) {
    size_t klen = strlen(key);
    for (size_t i = 0; i + klen + 2 < len; ++i) {
        if (json[i] == '"' && strncmp(json + i + 1, key, klen) == 0 && json[i + 1 + klen] == '"') {
            return json + i + 1 + klen + 1;
        }
    }
    return nullptr;
}

static int jsonParseInt(const char* p, const char** end = nullptr) {
    while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    int val = 0;
    while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
    if (end) *end = p;
    return val * sign;
}

static float jsonParseFloat(const char* p, const char** end = nullptr) {
    while (*p && (*p < '0' || *p > '9') && *p != '-' && *p != '.') p++;
    float val = strtof(p, (char**)&p);
    if (end) *end = p;
    return val;
}

static const char* jsonParseString(const char* p, const char** end) {
    while (*p && *p != '"') p++;
    if (*p != '"') return nullptr;
    p++;
    const char* start = p;
    while (*p && *p != '"') p++;
    if (end) *end = p;
    return start;
}

static const char* jsonSkipObject(const char* p) {
    int depth = 0;
    while (*p) {
        if (*p == '{') depth++;
        else if (*p == '}') { depth--; p++; if (depth == 0) return p; }
        else if (*p == '"') { p++; while (*p && *p != '"') p++; }
        p++;
    }
    return p;
}

static const char* jsonSkipArray(const char* p) {
    int depth = 0;
    while (*p) {
        if (*p == '[') depth++;
        else if (*p == ']') { depth--; p++; if (depth == 0) return p; }
        else if (*p == '"') { p++; while (*p && *p != '"') p++; }
        p++;
    }
    return p;
}

struct GLTFAccessor {
    int bufferView = -1;
    int componentType = 5126; // GL_FLOAT
    int count = 0;
    int typeComponents = 3; // SCALAR=1, VEC2=2, VEC3=3, VEC4=4, MAT4=16
    int byteOffset = 0;
};

struct GLTFBufferView {
    int buffer = 0;
    int byteOffset = 0;
    int byteLength = 0;
};

static int typeToComponents(const char* t) {
    if (strncmp(t, "SCALAR", 6) == 0) return 1;
    if (strncmp(t, "VEC2", 4) == 0) return 2;
    if (strncmp(t, "VEC3", 4) == 0) return 3;
    if (strncmp(t, "VEC4", 4) == 0) return 4;
    if (strncmp(t, "MAT4", 4) == 0) return 16;
    return 3;
}

static int componentTypeSize(int ct) {
    if (ct == 5120 || ct == 5121) return 1; // BYTE / UNSIGNED_BYTE
    if (ct == 5122 || ct == 5123) return 2; // SHORT / UNSIGNED_SHORT
    if (ct == 5125) return 4; // UNSIGNED_INT
    if (ct == 5126) return 4; // FLOAT
    return 4;
}

bool GLBLoader::load(const uint8_t* data, size_t len, GLBScene& outScene) {
    return parseGLB(data, len, outScene);
}

bool GLBLoader::parseGLB(const uint8_t* data, size_t len, GLBScene& outScene) {
    outScene = {};
    if (len < 12) return false;

    uint32_t magic, version, totalLen;
    readUint32LE(data + 0, magic);
    readUint32LE(data + 4, version);
    readUint32LE(data + 8, totalLen);

    if (magic != 0x46546C67) { // 'glTF'
        LOGE("Not a GLB file (magic=0x%08X)", magic);
        return false;
    }

    const char* jsonStr = nullptr;
    size_t jsonLen = 0;
    const uint8_t* binData = nullptr;
    size_t binLen = 0;

    size_t offset = 12;
    while (offset + 8 <= len) {
        uint32_t chunkLen, chunkType;
        readUint32LE(data + offset, chunkLen);
        readUint32LE(data + offset + 4, chunkType);
        offset += 8;
        if (offset + chunkLen > len) break;
        if (chunkType == 0x4E4F534A) { // JSON
            jsonStr = (const char*)(data + offset);
            jsonLen = chunkLen;
        } else if (chunkType == 0x004E4942) { // BIN
            binData = data + offset;
            binLen = chunkLen;
        }
        offset += chunkLen;
    }

    if (!jsonStr || !binData) {
        LOGE("Missing JSON or BIN chunk");
        return false;
    }

    // Parse accessors
    std::vector<GLTFAccessor> accessors;
    const char* accArr = jsonFindKey(jsonStr, jsonLen, "accessors");
    if (accArr) {
        accArr = strchr(accArr, '[');
        if (accArr) {
            accArr++;
            while (*accArr) {
                while (*accArr && *accArr != '{') accArr++;
                if (*accArr != '{') break;
                GLTFAccessor a{};
                const char* endObj = jsonSkipObject(accArr);
                const char* bv = jsonFindKey(accArr, endObj - accArr, "bufferView");
                if (bv) a.bufferView = jsonParseInt(bv);
                const char* ct = jsonFindKey(accArr, endObj - accArr, "componentType");
                if (ct) a.componentType = jsonParseInt(ct);
                const char* cnt = jsonFindKey(accArr, endObj - accArr, "count");
                if (cnt) a.count = jsonParseInt(cnt);
                const char* type = jsonFindKey(accArr, endObj - accArr, "type");
                if (type) {
                    const char* ts = nullptr;
                    jsonParseString(type, &ts);
                    if (ts) a.typeComponents = typeToComponents(ts);
                }
                const char* bo = jsonFindKey(accArr, endObj - accArr, "byteOffset");
                if (bo) a.byteOffset = jsonParseInt(bo);
                accessors.push_back(a);
                accArr = endObj;
            }
        }
    }

    // Parse bufferViews
    std::vector<GLTFBufferView> bufferViews;
    const char* bvArr = jsonFindKey(jsonStr, jsonLen, "bufferViews");
    if (bvArr) {
        bvArr = strchr(bvArr, '[');
        if (bvArr) {
            bvArr++;
            while (*bvArr) {
                while (*bvArr && *bvArr != '{') bvArr++;
                if (*bvArr != '{') break;
                GLTFBufferView bv{};
                const char* endObj = jsonSkipObject(bvArr);
                const char* b = jsonFindKey(bvArr, endObj - bvArr, "buffer");
                if (b) bv.buffer = jsonParseInt(b);
                const char* bo = jsonFindKey(bvArr, endObj - bvArr, "byteOffset");
                if (bo) bv.byteOffset = jsonParseInt(bo);
                const char* bl = jsonFindKey(bvArr, endObj - bvArr, "byteLength");
                if (bl) bv.byteLength = jsonParseInt(bl);
                bufferViews.push_back(bv);
                bvArr = endObj;
            }
        }
    }

    // Parse nodes
    const char* nodeArr = jsonFindKey(jsonStr, jsonLen, "nodes");
    if (nodeArr) {
        nodeArr = strchr(nodeArr, '[');
        if (nodeArr) {
            nodeArr++;
            while (*nodeArr) {
                while (*nodeArr && *nodeArr != '{') nodeArr++;
                if (*nodeArr != '{') break;
                GLBNode node{};
                const char* endObj = jsonSkipObject(nodeArr);
                const char* name = jsonFindKey(nodeArr, endObj - nodeArr, "name");
                if (name) {
                    const char* ns = nullptr;
                    jsonParseString(name, &ns);
                    if (ns) node.name = std::string(name + 1, ns - (name + 1));
                }
                const char* mesh = jsonFindKey(nodeArr, endObj - nodeArr, "mesh");
                if (mesh) node.meshIndex = jsonParseInt(mesh);
                const char* skin = jsonFindKey(nodeArr, endObj - nodeArr, "skin");
                if (skin) node.skinIndex = jsonParseInt(skin);
                const char* childrenKey = jsonFindKey(nodeArr, endObj - nodeArr, "children");
                if (childrenKey) {
                    childrenKey = strchr(childrenKey, '[');
                    if (childrenKey) {
                        childrenKey++;
                        while (*childrenKey && *childrenKey != ']') {
                            while (*childrenKey && (*childrenKey < '0' || *childrenKey > '9') && *childrenKey != '-' && *childrenKey != ']') {
                                childrenKey++;
                            }
                            if (*childrenKey == ']') break;
                            int childIdx = jsonParseInt(childrenKey, &childrenKey);
                            node.childrenIndices.push_back(childIdx);
                            while (*childrenKey && *childrenKey != ',' && *childrenKey != ']') {
                                childrenKey++;
                            }
                            if (*childrenKey == ',') childrenKey++;
                        }
                    }
                }
                const char* mat = jsonFindKey(nodeArr, endObj - nodeArr, "matrix");
                if (mat) {
                    mat = strchr(mat, '[');
                    if (mat) {
                        mat++;
                        for (int i = 0; i < 16; ++i) node.localMatrix[i] = jsonParseFloat(mat, &mat);
                    }
                } else {
                    // identity
                    for (int i = 0; i < 16; ++i) node.localMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
                }
                outScene.nodes.push_back(node);
                nodeArr = endObj;
            }
            // Resolve parentIndex for all nodes based on children indices
            for (size_t i = 0; i < outScene.nodes.size(); ++i) {
                for (int32_t childIdx : outScene.nodes[i].childrenIndices) {
                    if (childIdx >= 0 && childIdx < (int32_t)outScene.nodes.size()) {
                        outScene.nodes[childIdx].parentIndex = static_cast<int32_t>(i);
                    }
                }
            }
        }
    }

    // Parse skins
    const char* skinArr = jsonFindKey(jsonStr, jsonLen, "skins");
    if (skinArr) {
        skinArr = strchr(skinArr, '[');
        if (skinArr) {
            skinArr++;
            while (*skinArr) {
                while (*skinArr && *skinArr != '{') skinArr++;
                if (*skinArr != '{') break;
                GLBSkin skin{};
                const char* endObj = jsonSkipObject(skinArr);
                const char* name = jsonFindKey(skinArr, endObj - skinArr, "name");
                if (name) {
                    const char* ns = nullptr;
                    jsonParseString(name, &ns);
                    if (ns) skin.name = std::string(name + 1, ns - (name + 1));
                }
                const char* joints = jsonFindKey(skinArr, endObj - skinArr, "joints");
                if (joints) {
                    joints = strchr(joints, '[');
                    if (joints) {
                        joints++;
                        while (*joints) {
                            while (*joints && (*joints < '0' || *joints > '9') && *joints != '-') joints++;
                            if (*joints == ']' || *joints == '}') break;
                            skin.jointIndices.push_back(jsonParseInt(joints, &joints));
                            while (*joints && *joints != ',' && *joints != ']') joints++;
                            if (*joints == ']') break;
                        }
                    }
                }
                const char* ibm = jsonFindKey(skinArr, endObj - skinArr, "inverseBindMatrices");
                if (ibm) {
                    int accIdx = jsonParseInt(ibm);
                    if (accIdx >= 0 && accIdx < (int)accessors.size()) {
                        const GLTFAccessor& a = accessors[accIdx];
                        if (a.bufferView >= 0 && a.bufferView < (int)bufferViews.size()) {
                            const GLTFBufferView& bv = bufferViews[a.bufferView];
                            const uint8_t* src = binData + bv.byteOffset + a.byteOffset;
                            int floatsPerJoint = 16;
                            for (size_t j = 0; j < skin.jointIndices.size(); ++j) {
                                for (int k = 0; k < floatsPerJoint; ++k) {
                                    skin.inverseBindMatrices.push_back(readFloatLE(src));
                                    src += 4;
                                }
                            }
                        }
                    }
                }
                outScene.skins.push_back(skin);
                skinArr = endObj;
            }
        }
    }

    // Parse meshes
    const char* meshArr = jsonFindKey(jsonStr, jsonLen, "meshes");
    if (meshArr) {
        meshArr = strchr(meshArr, '[');
        if (meshArr) {
            meshArr++;
            while (*meshArr) {
                while (*meshArr && *meshArr != '{') meshArr++;
                if (*meshArr != '{') break;
                GLBMesh mesh{};
                const char* endMesh = jsonSkipObject(meshArr);
                const char* name = jsonFindKey(meshArr, endMesh - meshArr, "name");
                if (name) {
                    const char* ns = nullptr;
                    jsonParseString(name, &ns);
                    if (ns) mesh.name = std::string(name + 1, ns - (name + 1));
                }
                const char* primArr = jsonFindKey(meshArr, endMesh - meshArr, "primitives");
                if (primArr) {
                    primArr = strchr(primArr, '[');
                    if (primArr) {
                        primArr++;
                        while (*primArr) {
                            while (*primArr && *primArr != '{') primArr++;
                            if (*primArr != '{') break;
                            GLBPrimitive prim{};
                            const char* endPrim = jsonSkipObject(primArr);

                            const char* attrs = jsonFindKey(primArr, endPrim - primArr, "attributes");
                            int posAcc = -1, normAcc = -1, uvAcc = -1, jointAcc = -1, weightAcc = -1;
                            if (attrs) {
                                const char* p = jsonFindKey(attrs, endPrim - attrs, "POSITION");
                                if (p) posAcc = jsonParseInt(p);
                                p = jsonFindKey(attrs, endPrim - attrs, "NORMAL");
                                if (p) normAcc = jsonParseInt(p);
                                p = jsonFindKey(attrs, endPrim - attrs, "TEXCOORD_0");
                                if (p) uvAcc = jsonParseInt(p);
                                p = jsonFindKey(attrs, endPrim - attrs, "JOINTS_0");
                                if (p) jointAcc = jsonParseInt(p);
                                p = jsonFindKey(attrs, endPrim - attrs, "WEIGHTS_0");
                                if (p) weightAcc = jsonParseInt(p);
                            }

                            int idxAcc = -1;
                            const char* indices = jsonFindKey(primArr, endPrim - primArr, "indices");
                            if (indices) idxAcc = jsonParseInt(indices);

                            if (posAcc >= 0 && posAcc < (int)accessors.size()) {
                                const GLTFAccessor& a = accessors[posAcc];
                                int count = a.count;
                                prim.vertices.resize(count);
                                // Read positions
                                if (a.bufferView >= 0 && a.bufferView < (int)bufferViews.size()) {
                                    const GLTFBufferView& bv = bufferViews[a.bufferView];
                                    const uint8_t* src = binData + bv.byteOffset + a.byteOffset;
                                    for (int v = 0; v < count; ++v) {
                                        prim.vertices[v].pos[0] = readFloatLE(src); src += 4;
                                        prim.vertices[v].pos[1] = readFloatLE(src); src += 4;
                                        prim.vertices[v].pos[2] = readFloatLE(src); src += 4;
                                    }
                                }
                                // Read normals
                                if (normAcc >= 0 && normAcc < (int)accessors.size()) {
                                    const GLTFAccessor& a2 = accessors[normAcc];
                                    if (a2.bufferView >= 0 && a2.bufferView < (int)bufferViews.size()) {
                                        const GLTFBufferView& bv = bufferViews[a2.bufferView];
                                        const uint8_t* src = binData + bv.byteOffset + a2.byteOffset;
                                        for (int v = 0; v < count; ++v) {
                                            prim.vertices[v].normal[0] = readFloatLE(src); src += 4;
                                            prim.vertices[v].normal[1] = readFloatLE(src); src += 4;
                                            prim.vertices[v].normal[2] = readFloatLE(src); src += 4;
                                        }
                                    }
                                }
                                // Read UVs
                                if (uvAcc >= 0 && uvAcc < (int)accessors.size()) {
                                    const GLTFAccessor& a3 = accessors[uvAcc];
                                    if (a3.bufferView >= 0 && a3.bufferView < (int)bufferViews.size()) {
                                        const GLTFBufferView& bv = bufferViews[a3.bufferView];
                                        const uint8_t* src = binData + bv.byteOffset + a3.byteOffset;
                                        for (int v = 0; v < count; ++v) {
                                            prim.vertices[v].uv[0] = readFloatLE(src); src += 4;
                                            prim.vertices[v].uv[1] = readFloatLE(src); src += 4;
                                        }
                                    }
                                }
                                // Read joint indices (UNSIGNED_BYTE or UNSIGNED_SHORT)
                                if (jointAcc >= 0 && jointAcc < (int)accessors.size()) {
                                    const GLTFAccessor& a4 = accessors[jointAcc];
                                    if (a4.bufferView >= 0 && a4.bufferView < (int)bufferViews.size()) {
                                        const GLTFBufferView& bv = bufferViews[a4.bufferView];
                                        const uint8_t* src = binData + bv.byteOffset + a4.byteOffset;
                                        int stride = componentTypeSize(a4.componentType) * 4;
                                        for (int v = 0; v < count; ++v) {
                                            if (a4.componentType == 5121) { // UNSIGNED_BYTE
                                                prim.vertices[v].boneIndices[0] = src[0];
                                                prim.vertices[v].boneIndices[1] = src[1];
                                                prim.vertices[v].boneIndices[2] = src[2];
                                                prim.vertices[v].boneIndices[3] = src[3];
                                            } else if (a4.componentType == 5123) { // UNSIGNED_SHORT
                                                prim.vertices[v].boneIndices[0] = src[0] | (src[1] << 8);
                                                prim.vertices[v].boneIndices[1] = src[2] | (src[3] << 8);
                                                prim.vertices[v].boneIndices[2] = src[4] | (src[5] << 8);
                                                prim.vertices[v].boneIndices[3] = src[6] | (src[7] << 8);
                                            }
                                            src += stride;
                                        }
                                    }
                                }
                                // Read weights
                                if (weightAcc >= 0 && weightAcc < (int)accessors.size()) {
                                    const GLTFAccessor& a5 = accessors[weightAcc];
                                    if (a5.bufferView >= 0 && a5.bufferView < (int)bufferViews.size()) {
                                        const GLTFBufferView& bv = bufferViews[a5.bufferView];
                                        const uint8_t* src = binData + bv.byteOffset + a5.byteOffset;
                                        for (int v = 0; v < count; ++v) {
                                            prim.vertices[v].boneWeights[0] = readFloatLE(src); src += 4;
                                            prim.vertices[v].boneWeights[1] = readFloatLE(src); src += 4;
                                            prim.vertices[v].boneWeights[2] = readFloatLE(src); src += 4;
                                            prim.vertices[v].boneWeights[3] = readFloatLE(src); src += 4;
                                        }
                                    }
                                }
                            }

                            // Read indices
                            if (idxAcc >= 0 && idxAcc < (int)accessors.size()) {
                                const GLTFAccessor& ai = accessors[idxAcc];
                                if (ai.bufferView >= 0 && ai.bufferView < (int)bufferViews.size()) {
                                    const GLTFBufferView& bv = bufferViews[ai.bufferView];
                                    const uint8_t* src = binData + bv.byteOffset + ai.byteOffset;
                                    int sz = componentTypeSize(ai.componentType);
                                    for (int i = 0; i < ai.count; ++i) {
                                        if (sz == 1) {
                                            prim.indices.push_back(src[0]); src += 1;
                                        } else if (sz == 2) {
                                            prim.indices.push_back(src[0] | (src[1] << 8)); src += 2;
                                        } else {
                                            prim.indices.push_back(src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24)); src += 4;
                                        }
                                    }
                                }
                            }

                            mesh.primitives.push_back(prim);
                            primArr = endPrim;
                        }
                    }
                }
                outScene.meshes.push_back(mesh);
                meshArr = endMesh;
            }
        }
    }

    // Parse scene root
    const char* sceneKey = jsonFindKey(jsonStr, jsonLen, "scene");
    if (sceneKey) outScene.sceneRootNode = jsonParseInt(sceneKey);

    LOGI("GLB parsed: %zu nodes, %zu skins, %zu meshes", outScene.nodes.size(), outScene.skins.size(), outScene.meshes.size());
    return !outScene.meshes.empty();
}
