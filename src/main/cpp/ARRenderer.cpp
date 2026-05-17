#include "ARRenderer.h"
#include "Shader.h"
#include "Mesh.h"
#include <android/log.h>
#include <cmath>

#define LOG_TAG "ARRenderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static const char* CAMERA_VERT = R"(
    attribute vec4 a_Position;
    attribute vec2 a_TexCoord;
    varying vec2 v_TexCoord;
    void main() {
        gl_Position = a_Position;
        v_TexCoord = a_TexCoord;
    }
)";

static const char* CAMERA_FRAG = R"(
    #extension GL_OES_EGL_image_external : require
    precision mediump float;
    uniform samplerExternalOES u_Texture;
    varying vec2 v_TexCoord;
    void main() {
        gl_FragColor = texture2D(u_Texture, v_TexCoord);
    }
)";

static const char* GAME_VERT = R"(
    precision mediump float;
    attribute vec3 a_Position;
    attribute vec3 a_Normal;
    varying vec3 v_Normal;
    uniform mat4 u_ModelViewProj;
    void main() {
        v_Normal = a_Normal;
        gl_Position = u_ModelViewProj * vec4(a_Position, 1.0);
    }
)";

static const char* GAME_FRAG = R"(
    precision mediump float;
    varying vec3 v_Normal;
    uniform vec3 u_Color;
    void main() {
        float light = max(dot(normalize(v_Normal), vec3(0.0, 1.0, 0.0)), 0.3);
        gl_FragColor = vec4(u_Color * light, 1.0);
    }
)";

static void mat4Mul(const float* a, const float* b, float* out) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out[i*4+j] = a[i*4+0]*b[0*4+j] + a[i*4+1]*b[1*4+j]
                        + a[i*4+2]*b[2*4+j] + a[i*4+3]*b[3*4+j];
}

void ARRenderer::init() {
    cameraShader_ = Shader::compile(CAMERA_VERT, CAMERA_FRAG);
    gameShader_   = Shader::compile(GAME_VERT, GAME_FRAG);

    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadPositions_), quadPositions_, GL_STATIC_DRAW);

    // Init placeholder meshes
    playerMesh_.loadCube(0.15f);
    ballMesh_.loadSphere(0.08f, 8, 8);
    pitchMesh_.loadCube(1.0f);
}

void ARRenderer::destroy() {
    Shader::destroy(cameraShader_);
    Shader::destroy(gameShader_);
    if (quadVbo_) glDeleteBuffers(1, &quadVbo_);
    playerMesh_.destroy();
    ballMesh_.destroy();
    pitchMesh_.destroy();
}

void ARRenderer::drawCameraBackground(ARManager& ar) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    Shader::use(cameraShader_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, ar.getCameraTextureId());
    Shader::setInt(cameraShader_, "u_Texture", 0);

    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    static const float uvs[8] = {0, 1, 1, 1, 0, 0, 1, 0};
    GLuint uvVbo;
    glGenBuffers(1, &uvVbo);
    glBindBuffer(GL_ARRAY_BUFFER, uvVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(uvs), uvs, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDeleteBuffers(1, &uvVbo);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

void ARRenderer::renderGameOnMarker(ARManager& ar, const float* playerPositions, int numPlayers) {
    if (!ar.isMarkerTracked()) return;

    ARPose anchorPose = ar.getMarkerAnchorPose();
    if (!anchorPose.valid) return;

    float view[16], proj[16];
    ar.getViewMatrix(view);
    ar.getProjectionMatrix(proj, 0.01f, 100.0f);

    Shader::use(gameShader_);
    GLint mvpLoc = glGetUniformLocation(gameShader_, "u_ModelViewProj");
    GLint colLoc = glGetUniformLocation(gameShader_, "u_Color");

    // Render pitch
    float pitchM[16], tmp[16], mvp[16];
    for (int j=0;j<16;++j) pitchM[j] = (j%5==0)?1.0f:0.0f;
    pitchM[12] = 0.0f; pitchM[13] = -0.01f; pitchM[14] = 0.0f;
    pitchM[0] = 1.05f; pitchM[5] = 0.02f; pitchM[10] = 0.68f;  // scale
    mat4Mul(view, anchorPose.matrix, tmp);
    mat4Mul(tmp, pitchM, mvp);
    mat4Mul(proj, mvp, tmp);
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, tmp);
    glUniform3f(colLoc, 0.15f, 0.45f, 0.15f);
    pitchMesh_.draw();

    // Render players
    for (int i = 0; i < numPlayers; ++i) {
        float model[16];
        for (int j = 0; j < 16; ++j) model[j] = (j % 5 == 0) ? 1.0f : 0.0f;
        model[12] = playerPositions[i * 3 + 0];
        model[13] = playerPositions[i * 3 + 1] + 0.075f;  // half height offset
        model[14] = playerPositions[i * 3 + 2];

        mat4Mul(view, anchorPose.matrix, tmp);
        mat4Mul(tmp, model, mvp);
        mat4Mul(proj, mvp, tmp);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, tmp);

        bool teamA = (i < 11);
        glUniform3f(colLoc, teamA ? 0.0f : 1.0f, teamA ? 0.2f : 0.0f, teamA ? 0.8f : 0.0f);
        playerMesh_.draw();
    }

    // Render ball
    float ballM[16];
    for (int j=0;j<16;++j) ballM[j]=(j%5==0)?1.0f:0.0f;
    // ball pos from last player for demo
    ballM[12] = playerPositions[0]; ballM[13] = 0.08f; ballM[14] = playerPositions[2];
    mat4Mul(view, anchorPose.matrix, tmp);
    mat4Mul(tmp, ballM, mvp);
    mat4Mul(proj, mvp, tmp);
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, tmp);
    glUniform3f(colLoc, 1.0f, 0.9f, 0.1f);
    ballMesh_.draw();
}
