#include "ARRenderer.h"
#include "Shader.h"
#include "Mesh.h"
#include <android/log.h>
#include <cmath>
#include <cstring>

#define LOG_TAG "ARRenderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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
uniform mat4 u_BoneMatrices[16];

out vec3 v_Normal;
out vec2 v_TexCoord;

void main() {
    mat4 skinMatrix =
        u_BoneMatrices[a_BoneIndices.x] * a_BoneWeights.x +
        u_BoneMatrices[a_BoneIndices.y] * a_BoneWeights.y +
        u_BoneMatrices[a_BoneIndices.z] * a_BoneWeights.z +
        u_BoneMatrices[a_BoneIndices.w] * a_BoneWeights.w;

    vec4 skinnedPos = skinMatrix * vec4(a_Position, 1.0);
    vec4 skinnedNormal = skinMatrix * vec4(a_Normal, 0.0);

    v_Normal = normalize(skinnedNormal.xyz);
    v_TexCoord = a_TexCoord;
    gl_Position = u_ModelViewProj * skinnedPos;
}
)";

static const char* SKINNED_FRAG = R"(#version 300 es
precision mediump float;
in vec3 v_Normal;
in vec2 v_TexCoord;
out vec4 outColor;
uniform vec3 u_Color;
void main() {
    vec3 N = normalize(v_Normal);
    vec3 L = normalize(vec3(0.2, 1.0, 0.3));
    float diff = max(dot(N, L), 0.0);
    float amb = 0.35;
    outColor = vec4(u_Color * (diff + amb), 1.0);
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
void main() {
    v_Normal = a_Normal;
    v_TexCoord = a_TexCoord;
    gl_Position = u_ModelViewProj * vec4(a_Position, 1.0);
}
)";

static const char* STATIC_FRAG = R"(#version 300 es
precision mediump float;
in vec3 v_Normal;
in vec2 v_TexCoord;
out vec4 outColor;
uniform vec3 u_Color;
void main() {
    vec3 N = normalize(v_Normal);
    vec3 L = normalize(vec3(0.2, 1.0, 0.3));
    float diff = max(dot(N, L), 0.0);
    float amb = 0.35;
    outColor = vec4(u_Color * (diff + amb), 1.0);
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
    int pitch = scene_.addNode("pitch", root);
    scene_.nodes[pitch].staticMesh.loadCube(1.0f);
    scene_.nodes[pitch].local.scale[0] = 11.0f;
    scene_.nodes[pitch].local.scale[1] = 0.25f;
    scene_.nodes[pitch].local.scale[2] = 5.0f;
    scene_.nodes[pitch].local.position[1] = -0.1f;

    int goalL = scene_.addNode("goalL", root);
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

    int ball = scene_.addNode("ball", root);
    scene_.nodes[ball].staticMesh.loadSphere(0.25f, 12, 12);
    scene_.nodes[ball].local.position[1] = 0.25f;

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
                               const float* boneMatrices, int numBones) {
    bool tracked = ar.isMarkerTracked();
    ARPose anchorPose = ar.getMarkerAnchorPose();
    float fallbackAnchor[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0.0f, -1.5f, -8.0f, 1.0f
    };
    const float* anchorMat = anchorPose.valid ? anchorPose.matrix : fallbackAnchor;

    float view[16], proj[16];
    ar.getViewMatrix(view);
    ar.getProjectionMatrix(proj, 0.01f, 100.0f);

    // Combine ARCore view/proj with marker anchor
    float vp[16];
    mat4Mul(proj, view, vp);

    // Update scene graph root to anchor
    int rootIdx = scene_.findNode("root");
    if (rootIdx >= 0) {
        std::memcpy(scene_.nodes[rootIdx].worldMatrix, anchorMat, 16*sizeof(float));
        scene_.update();
    }

    renderStaticObjects(vp);
    renderPlayers(vp, playerPositions, numPlayers, boneMatrices, numBones);
}

void ARRenderer::renderStaticObjects(const float* viewProj) {
    Shader::use(staticShader_);
    GLint mvpLoc = glGetUniformLocation(staticShader_, "u_ModelViewProj");
    GLint colLoc = glGetUniformLocation(staticShader_, "u_Color");

    for (auto& node : scene_.nodes) {
        if (node.useSkinning || !node.visible) continue;
        if (!node.staticMesh.hasData()) continue;

        float mvp[16];
        mat4Mul(viewProj, node.worldMatrix, mvp);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

        if (node.name == "pitch") glUniform3f(colLoc, 0.15f, 0.45f, 0.15f);
        else if (node.name.find("goal") == 0) glUniform3f(colLoc, 0.9f, 0.9f, 0.9f);
        else if (node.name == "ball") glUniform3f(colLoc, 1.0f, 0.9f, 0.1f);
        else glUniform3f(colLoc, 1.0f, 1.0f, 1.0f);

        node.staticMesh.draw();
    }
}

void ARRenderer::renderPlayers(const float* viewProj, const float* playerPositions, int numPlayers,
                              const float* boneMatrices, int numBones) {
    int baseIdx = scene_.findNode("playerBase");
    if (baseIdx < 0 || !scene_.nodes[baseIdx].skinnedMesh.hasData()) return;

    Shader::use(skinnedShader_);
    GLint mvpLoc = glGetUniformLocation(skinnedShader_, "u_ModelViewProj");
    GLint colLoc = glGetUniformLocation(skinnedShader_, "u_Color");
    GLint boneLoc = glGetUniformLocation(skinnedShader_, "u_BoneMatrices");

    if (boneMatrices && numBones > 0) {
        glUniformMatrix4fv(boneLoc, numBones, GL_FALSE, boneMatrices);
    }

    for (int i = 0; i < numPlayers; ++i) {
        float model[16];
        mat4Identity(model);
        model[12] = playerPositions[i * 3 + 0];
        model[13] = playerPositions[i * 3 + 1] + 0.3f;
        model[14] = playerPositions[i * 3 + 2];

        float mvp[16];
        mat4Mul(viewProj, model, mvp);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

        bool teamA = (i < 11);
        glUniform3f(colLoc, teamA ? 0.0f : 1.0f, teamA ? 0.5f : 0.0f, teamA ? 1.0f : 0.0f);
        scene_.nodes[baseIdx].skinnedMesh.draw();
    }
}
 
