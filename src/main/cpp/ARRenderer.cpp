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

// Column-major matrix multiplication: out = a * b
// Where m[col*4+row] = m(row, col) in column-major storage.
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

void ARRenderer::init() {
    cameraShader_ = Shader::compile(CAMERA_VERT, CAMERA_FRAG);
    gameShader_   = Shader::compile(GAME_VERT, GAME_FRAG);

    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadPositions_), quadPositions_, GL_STATIC_DRAW);

    // Init placeholder meshes - larger for TV broadcast view
    playerMesh_.loadCube(0.6f);
    ballMesh_.loadSphere(0.25f, 12, 12);
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
    if (ar.getCameraTextureId() == 0) return; // No camera in fallback mode
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

// Forward decl: fallback view (TV broadcast) used when no AR marker is tracked
static void buildFallbackView(float* m);

void ARRenderer::renderGameOnMarker(ARManager& ar, const float* playerPositions, int numPlayers) {
    bool tracked = ar.isMarkerTracked();
    ARPose anchorPose = ar.getMarkerAnchorPose();
    // Fallback anchor when no real marker: place the pitch in front of and
    // below the AR camera so it's visible from the device's start pose.
    // Column-major translation matrix.
    float fallbackAnchor[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0.0f, -1.5f, -8.0f, 1.0f   // 8m in front (-Z), 1.5m below (-Y)
    };
    const float* anchorMat = anchorPose.valid ? anchorPose.matrix : fallbackAnchor;

    float view[16], proj[16];
    // ARManager.getViewMatrix returns the real ARCore camera pose if session is
    // alive (so moving the emulator/device updates it). It falls back to a
    // fixed broadcast lookAt only when no AR session exists at all.
    ar.getViewMatrix(view);
    ar.getProjectionMatrix(proj, 0.01f, 100.0f);

    Shader::use(gameShader_);
    GLint mvpLoc = glGetUniformLocation(gameShader_, "u_ModelViewProj");
    GLint colLoc = glGetUniformLocation(gameShader_, "u_Color");

    // mat4Mul(A, B, OUT) computes OUT = A * B in column-major.
    // Final MVP = proj * view * anchor * model.

    // Render pitch
    float pitchM[16], tmp[16], mvp[16];
    for (int j=0;j<16;++j) pitchM[j] = (j%5==0)?1.0f:0.0f;
    pitchM[12] = 0.0f; pitchM[13] = -0.1f; pitchM[14] = 0.0f;
    pitchM[0] = 11.0f; pitchM[5] = 0.25f; pitchM[10] = 5.0f;  // scale: 22m x 0.5m x 10m
    mat4Mul(view, anchorMat, tmp);     // tmp = view * anchor
    mat4Mul(tmp, pitchM, mvp);         // mvp = view * anchor * model
    mat4Mul(proj, mvp, tmp);           // tmp = proj * view * anchor * model
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, tmp);
    glUniform3f(colLoc, 0.15f, 0.45f, 0.15f);
    pitchMesh_.draw();

    // Render players
    for (int i = 0; i < numPlayers; ++i) {
        float model[16];
        for (int j = 0; j < 16; ++j) model[j] = (j % 5 == 0) ? 1.0f : 0.0f;
        model[12] = playerPositions[i * 3 + 0];
        model[13] = playerPositions[i * 3 + 1] + 0.3f;
        model[14] = playerPositions[i * 3 + 2];

        mat4Mul(view, anchorMat, tmp);
        mat4Mul(tmp, model, mvp);
        mat4Mul(proj, mvp, tmp);
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, tmp);

        bool teamA = (i < 11);
        glUniform3f(colLoc, teamA ? 0.0f : 1.0f, teamA ? 0.5f : 0.0f, teamA ? 1.0f : 0.0f);
        playerMesh_.draw();
    }

    // Render ball
    float ballM[16];
    for (int j=0;j<16;++j) ballM[j]=(j%5==0)?1.0f:0.0f;
    ballM[12] = 0.0f; ballM[13] = 0.25f; ballM[14] = 0.0f;
    mat4Mul(view, anchorMat, tmp);
    mat4Mul(tmp, ballM, mvp);
    mat4Mul(proj, mvp, tmp);
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, tmp);
    glUniform3f(colLoc, 1.0f, 0.9f, 0.1f);
    ballMesh_.draw();
}

// Build a column-major lookAt view for the TV broadcast fallback view
static void buildFallbackView(float* m) {
    // Camera position: above and behind looking at origin
    float ex = 0.0f, ey = 8.0f, ez = 14.0f;
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    float upx = 0.0f, upy = 1.0f, upz = 0.0f;

    // forward = normalize(center - eye)
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx*fx + fy*fy + fz*fz);
    fx /= fl; fy /= fl; fz /= fl;

    // s = normalize(forward x up)
    float sx = fy*upz - fz*upy;
    float sy = fz*upx - fx*upz;
    float sz = fx*upy - fy*upx;
    float sl = sqrtf(sx*sx + sy*sy + sz*sz);
    sx /= sl; sy /= sl; sz /= sl;

    // u = s x forward
    float ux = sy*fz - sz*fy;
    float uy = sz*fx - sx*fz;
    float uz = sx*fy - sy*fx;

    // Column-major: m[col*4+row]
    m[0]=sx;   m[1]=ux;   m[2]=-fx;  m[3]=0;
    m[4]=sy;   m[5]=uy;   m[6]=-fy;  m[7]=0;
    m[8]=sz;   m[9]=uz;   m[10]=-fz; m[11]=0;
    m[12]=-(sx*ex + sy*ey + sz*ez);
    m[13]=-(ux*ex + uy*ey + uz*ez);
    m[14]= (fx*ex + fy*ey + fz*ez);
    m[15]=1;
}
 
