#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <GLES3/gl3.h>
#include <vector>
#include <cmath>

#include "ARManager.h"
#include "ARRenderer.h"
#include "GameBridge.h"
#include "TouchController.h"
#include "AnimationPlayer.h"
#include "AssetLoader.h"
#include "protocol/DZFootProtocol.h"
#include <cstring>
#include <cstdio>

#define LOG_TAG "DZFootJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static const char* roleToString(uint8_t role) {
    switch (role) {
        case 0: return "GK";
        case 1: return "CB";
        case 2: return "LB";
        case 3: return "RB";
        case 4: return "DM";
        case 5: return "CM";
        case 6: return "LM";
        case 7: return "RM";
        case 8: return "AM";
        case 9: return "CF";
        default: return "??";
    }
}

static const char* animToString(uint8_t anim) {
    switch (anim) {
        case 0: return "IDLE";
        case 1: return "WALK";
        case 2: return "RUN";
        case 3: return "SPRINT";
        case 4: return "SHOOT_R";
        case 5: return "SHOOT_L";
        case 6: return "PASS_S";
        case 7: return "PASS_L";
        case 8: return "HEADER";
        case 9: return "TACKLE";
        case 10: return "DRIBBLE";
        case 11: return "FALL";
        case 12: return "CELEBRATE";
        case 13: return "GK_IDLE";
        case 14: return "GK_DIVE_L";
        case 15: return "GK_DIVE_R";
        case 16: return "GK_CATCH";
        default: return "???";
    }
}

static constexpr const char* NATIVE_BUILD_MARKER = "DZFOOT_VERIFY_2026_06_15_0014";

// Forward declaration of protocol test (tests/test_protocol_layout.cpp)
extern bool runProtocolTests();

static ARManager gArManager;
static ARRenderer gRenderer;
static GameBridge gGameBridge;
static TouchController gTouchController;
AnimationPlayer gAnimPlayer;
AAssetManager* gAssetManager = nullptr;
static bool gRendererInited = false;
static bool gAnimLoaded = false;
static int gScreenW = 1080, gScreenH = 1920;
jobject gActivityObj = nullptr;
JavaVM* gJavaVM = nullptr;

// Last known camera forward on XZ plane (updated each frame, used for camera-relative controls)
static float gCamFwdX = 0.0f;
static float gCamFwdZ = 1.0f;

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    gJavaVM = vm;
    LOGI("%s JNI_OnLoad", NATIVE_BUILD_MARKER);
    return JNI_VERSION_1_6;
}

JNIEXPORT jboolean JNICALL
Java_com_football_ar_JniBridge_nativeInit(JNIEnv* env, jobject thiz, jobject context, jobject assetManager, jboolean isEmulator) {
    LOGI("%s nativeInit", NATIVE_BUILD_MARKER);
    gAssetManager = AAssetManager_fromJava(env, assetManager);
    if (gActivityObj) {
        env->DeleteGlobalRef(gActivityObj);
        gActivityObj = nullptr;
    }
    gActivityObj = env->NewGlobalRef(context); // MainActivity, has audio methods
    if (!runProtocolTests()) {
        LOGI("Protocol test FAILED — binary layout mismatch detected");
    }
    if (!gArManager.init(env, context, gAssetManager, isEmulator == JNI_TRUE)) {
        LOGI("ARManager init failed");
        return JNI_FALSE;
    }
    LOGI("Native init OK (emulator=%d)", isEmulator);
    return JNI_TRUE;
}

JNIEXPORT jstring JNICALL
Java_com_football_ar_JniBridge_nativeGetBuildMarker(JNIEnv* env, jobject thiz) {
    return env->NewStringUTF(NATIVE_BUILD_MARKER);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeDestroy(JNIEnv* env, jobject thiz) {
    if (gActivityObj) {
        env->DeleteGlobalRef(gActivityObj);
        gActivityObj = nullptr;
    }
    gRenderer.destroy();
    gRendererInited = false;
    gArManager.destroy();
    LOGI("Native destroy OK");
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeResume(JNIEnv* env, jobject thiz, jobject context) {
    gArManager.onResume(env, context, nullptr);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativePause(JNIEnv* env, jobject thiz) {
    gArManager.onPause();
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeSurfaceCreated(JNIEnv* env, jobject thiz) {
    gArManager.onSurfaceCreated();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);  // Disabled: simpler debug, can enable once winding verified
    
    if (gRendererInited) {
        gRenderer.destroy();
        gRendererInited = false;
    }
    gRenderer.init();
    gRendererInited = true;
    LOGI("%s Renderer init OK (GL context ready)", NATIVE_BUILD_MARKER);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeDisplayChanged(
    JNIEnv* env, jobject thiz, jint rotation, jint width, jint height) {
    gArManager.onDisplayGeometryChanged(rotation, width, height);
    gScreenW = width; gScreenH = height;
    gTouchController.setScreenSize(width, height);
    glViewport(0, 0, width, height);
    LOGI("Viewport set: %dx%d rotation=%d", width, height, rotation);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeOnFrame(
    JNIEnv* env, jobject thiz,
    jfloatArray viewMat, jfloatArray projMat, jfloatArray anchorMat,
    jbyteArray gameStateData) {

    if (!gArManager.update()) return;

    static int frameNum = 0;
    frameNum++;

    // Clear screen (vivid stadium sky blue gradient)
    glClearColor(0.45f, 0.72f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Apply game state if present and not empty (must happen BEFORE getViewMatrix
    // so the broadcast camera can focus on the current ball position)
    if (gameStateData && env->GetArrayLength(gameStateData) > 0) {
        jbyte* state = env->GetByteArrayElements(gameStateData, nullptr);
        gGameBridge.applyGameState((const uint8_t*)state, env->GetArrayLength(gameStateData));
        env->ReleaseByteArrayElements(gameStateData, state, JNI_ABORT);
    }

    // Point the broadcast camera at the ball+active player for TV drama
    // Mimics GameplayFootball PC: target = ball*(1-bias) + player*bias
    {
        dzfoot::GameStatePacket peek = gGameBridge.getInterpolatedState();
        const float kScaleX = gRenderer.getPitchScaleX();
        const float kScaleZ = gRenderer.getPitchScaleZ();
        
        // Find active player (flag bit 2 = controlled)
        int activeIdx = -1;
        for (int i = 0; i < 22; ++i) {
            if (peek.players[i].flags & 4) { activeIdx = i; break; }
        }
        float playerX = 0.0f, playerZ = 0.0f;
        if (activeIdx >= 0) {
            playerX = peek.players[activeIdx].pos[0] * kScaleX;
            playerZ = peek.players[activeIdx].pos[1] * kScaleZ;
        }
        // Ball speed for camera shudder intensity
        float ballVx = peek.ball.vel[0];
        float ballVy = peek.ball.vel[1];
        float ballSpeed = std::sqrt(ballVx * ballVx + ballVy * ballVy);
        
        gArManager.setCameraFocus(
            peek.ball.pos[0] * kScaleX, peek.ball.pos[1] * kScaleZ,
            playerX, playerZ, ballSpeed);

        // Broadcast camera must know actual scene extents to clamp/position itself
        // proportionally, just like player and ball rendering does.
        gArManager.setPitchExtents(gRenderer.getSceneHalfX(), gRenderer.getSceneHalfZ());

        if (gArManager.getCameraMode() == CameraMode::Classic) {
            // Force broadcast TV camera (defined in ARManager.cpp) instead of server camera
            gArManager.clearServerCamera();
        } else {
            gArManager.setServerCamera(
                peek.camera.pos[0] * kScaleX,
                peek.camera.pos[2] * 0.1f + 0.08f,
                peek.camera.pos[1] * kScaleZ,
                peek.camera.fov);
        }

        // Log interpolated render state for diagnostics
        static int renderLogCounter = 0;
        if ((renderLogCounter++ % 120) == 0) {
            int activeRenderIdx = -1;
            for (int i = 0; i < 22; ++i) {
                if (peek.players[i].flags & 4) { activeRenderIdx = i; break; }
            }
            char rbuf[2048];
            int roff = 0;
            roff += snprintf(rbuf + roff, sizeof(rbuf) - roff, "[gamestates] RENDER tick=%u ball=(%.2f,%.2f,%.2f) cam=(%.2f,%.2f,%.2f) active=%d ",
                               peek.tick, peek.ball.pos[0], peek.ball.pos[1], peek.ball.pos[2],
                               peek.camera.pos[0], peek.camera.pos[1], peek.camera.pos[2], activeRenderIdx);
            for (int i = 0; i < 22 && roff < (int)sizeof(rbuf) - 60; ++i) {
                roff += snprintf(rbuf + roff, sizeof(rbuf) - roff, "p%d=(%.2f,%.2f,a=%u,rY=%.1f,f=%u) ",
                                 i, peek.players[i].pos[0], peek.players[i].pos[1],
                                 peek.players[i].anim, peek.players[i].rotY, peek.players[i].flags);
            }
            LOGI("%s", rbuf);
        }
    }

    // Get AR matrices
    jfloat* view = env->GetFloatArrayElements(viewMat, nullptr);
    jfloat* proj = env->GetFloatArrayElements(projMat, nullptr);
    jfloat* anchor = env->GetFloatArrayElements(anchorMat, nullptr);
    gArManager.getViewMatrix(view);
    gArManager.getProjectionMatrix(proj, 0.01f, 100.0f);

    // Extract camera forward on XZ plane from view matrix for camera-relative controls.
    // OpenGL view matrix row 2 = -forward (world space), elements [2] and [10] are X and Z.
    {
        float vfwdX = -view[2];
        float vfwdZ = -view[10];
        float vlen = std::sqrt(vfwdX * vfwdX + vfwdZ * vfwdZ);
        if (vlen > 0.0001f) {
            gCamFwdX = vfwdX / vlen;
            gCamFwdZ = vfwdZ / vlen;
        }
    }

    ARPose pose = gArManager.getMarkerAnchorPose();
    if (pose.valid) {
        std::memcpy(anchor, pose.matrix, 16 * sizeof(float));
    }
    env->ReleaseFloatArrayElements(viewMat, view, 0);
    env->ReleaseFloatArrayElements(projMat, proj, 0);
    env->ReleaseFloatArrayElements(anchorMat, anchor, 0);

    // (GameState already applied above, before the camera view was computed.)

    // Render camera background
    gRenderer.drawCameraBackground(gArManager);

    // No offline mode — GF is the single source of truth.
    // If no game state received, render nothing (pitch stays empty).
    dzfoot::GameStatePacket gs;
    if (!gameStateData || env->GetArrayLength(gameStateData) == 0) {
        return;
    }
    gs = gGameBridge.getInterpolatedState();

    // Detect active player (flags bit 2 = designated/controlled) and sync with controller
    {
        int activeIdx = -1;
        for (int i = 0; i < 22; ++i) {
            if (gs.players[i].flags & 4) { activeIdx = i; break; }
        }
        if (activeIdx >= 0) {
            uint8_t team = gs.players[activeIdx].team;
            uint8_t playerIdx = static_cast<uint8_t>(activeIdx % 11);
            gTouchController.setActivePlayer(team, playerIdx);
        } else {
            // No active player designated by GF yet (e.g. before kickoff)
            gTouchController.setActivePlayer(0, 0);
        }
    }

    float positions[75]; // 25 entities * 3 (22 players + 3 officials)
    uint8_t playerAnims[25];
    float playerVels[75];
    float playerRotY[25];
    uint8_t playerFlags[25];
    uint8_t playerTeams[25];
    uint8_t playerRoles[25];
    for (int i = 0; i < 22; ++i) {
        positions[i * 3 + 0] = gs.players[i].pos[0];
        positions[i * 3 + 1] = gs.players[i].pos[1];
        positions[i * 3 + 2] = gs.players[i].pos[2];
        playerAnims[i] = gs.players[i].anim;
        
        // Pass stable direction vector for heading angle selection, and actual scalar speed.
        // Server vel is noisy/small; dir is stable and computed on the server from physical direction.
        float vx = gs.players[i].vel[0];
        float vy = gs.players[i].vel[1]; // width
        float speed = std::sqrt(vx * vx + vy * vy);
        playerVels[i * 3 + 0] = gs.players[i].dir[0];
        playerVels[i * 3 + 1] = gs.players[i].dir[1];
        playerVels[i * 3 + 2] = speed;
        
        playerRotY[i] = gs.players[i].rotY;
        playerFlags[i] = gs.players[i].flags;
        playerTeams[i] = gs.players[i].team;
        playerRoles[i] = gs.players[i].role;
    }
    // Add officials (referee + linesmen)
    for (int i = 0; i < 3; ++i) {
        int idx = 22 + i;
        positions[idx * 3 + 0] = gs.officials[i].pos[0];
        positions[idx * 3 + 1] = gs.officials[i].pos[1];
        positions[idx * 3 + 2] = gs.officials[i].pos[2];
        playerAnims[idx] = gs.officials[i].anim;
        
        playerVels[idx * 3 + 0] = gs.officials[i].dir[0];
        playerVels[idx * 3 + 1] = gs.officials[i].dir[1];
        playerVels[idx * 3 + 2] = 0.0f; // ignored for officials
        
        playerRotY[idx] = gs.officials[i].rotY;
        playerFlags[idx] = gs.officials[i].flags;
        playerTeams[idx] = gs.officials[i].team; // 2 = officials team
        playerRoles[idx] = gs.officials[i].role; // 0=referee, 1=linesmanN, 2=linesmanS
    }
    float ballPos[3] = { gs.ball.pos[0], gs.ball.pos[1], gs.ball.pos[2] };

    gTouchController.updateRadar(gs);
    gRenderer.renderScene(gArManager, positions, 25, ballPos,
                          nullptr, 0, playerAnims, playerVels, playerRotY,
                          playerFlags, playerTeams, playerRoles,
                          &gTouchController, gScreenW, gScreenH);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeOnTouch(
    JNIEnv* env, jobject thiz, jfloat x, jfloat y, jint action, jint pointerId) {
    LOGI("[DZ_JNI] nativeOnTouch action=%d x=%.1f y=%.1f pid=%d", action, x, y, pointerId);
    if (action == 0 || action == 5) { // ACTION_DOWN or ACTION_POINTER_DOWN
        gTouchController.onTouchDown(pointerId, x, y);
    } else if (action == 2) { // ACTION_MOVE
        gTouchController.onTouchMove(pointerId, x, y);
    } else if (action == 1 || action == 6 || action == 3) { // ACTION_UP, ACTION_POINTER_UP or ACTION_CANCEL
        gTouchController.onTouchUp(pointerId);
    }
}

JNIEXPORT jbyteArray JNICALL
Java_com_football_ar_JniBridge_nativeGetInputBytes(JNIEnv* env, jobject thiz) {
    // Rotate input by last known camera direction so controls are camera-relative
    gTouchController.applyCameraRotation(gCamFwdX, gCamFwdZ);
    constexpr size_t pktSize = sizeof(dzfoot::PlayerInputPacket);
    uint8_t buf[pktSize];
    gTouchController.serialize(buf, pktSize);
    const dzfoot::PlayerInputPacket* pkt = reinterpret_cast<const dzfoot::PlayerInputPacket*>(buf);
    bool hasInput = (pkt->buttons != 0) || (std::fabs(pkt->dirX) > 0.01f) || (std::fabs(pkt->dirZ) > 0.01f);
    if (hasInput) {
        LOGI("[DZ_JNI] JNI_INPUT team=%u player=%u dir=(%.3f,%.3f) buttons=0x%04X",
             pkt->team, pkt->playerIdx, pkt->dirX, pkt->dirZ, pkt->buttons);
    }
    jbyteArray result = env->NewByteArray(static_cast<jsize>(pktSize));
    env->SetByteArrayRegion(result, 0, static_cast<jsize>(pktSize), (jbyte*)buf);
    return result;
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeSetActionKick(JNIEnv* env, jobject thiz, jboolean on) {
    gTouchController.setActionKick(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeSetActionPass(JNIEnv* env, jobject thiz, jboolean on) {
    gTouchController.setActionPass(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeSetActionShot(JNIEnv* env, jobject thiz, jboolean on) {
    gTouchController.setActionShot(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeSetActionDribble(JNIEnv* env, jobject thiz, jboolean on) {
    gTouchController.setActionDribble(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeSetSprint(JNIEnv* env, jobject thiz, jboolean on) {
    gTouchController.setSprint(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeOnGameStateReceived(
    JNIEnv* env, jobject thiz, jbyteArray data) {
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    gGameBridge.applyGameState((const uint8_t*)bytes, env->GetArrayLength(data));
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeOnGameEvent(
    JNIEnv* env, jobject thiz, jbyteArray data) {
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    gGameBridge.applyMatchEvent((const uint8_t*)bytes, env->GetArrayLength(data));
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeOnTacticalState(
    JNIEnv* env, jobject thiz, jbyteArray data) {
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    gGameBridge.applyTacticalState((const uint8_t*)bytes, env->GetArrayLength(data));
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeOnMatchSetup(
    JNIEnv* env, jobject thiz, jbyteArray data) {
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    gGameBridge.applyMatchSetup((const uint8_t*)bytes, env->GetArrayLength(data));
    gRenderer.setMatchSetup(gGameBridge.matchSetup());
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

} // extern "C"

// ─── Audio helper used by AudioManager.cpp ─────────────────────

static JNIEnv* getJniEnv() {
    JNIEnv* env = nullptr;
    if (gJavaVM && gJavaVM->AttachCurrentThread(&env, nullptr) == JNI_OK) {
        return env;
    }
    return nullptr;
}

void androidPlaySound(const char* name) {
    JNIEnv* env = getJniEnv();
    if (!env || !gActivityObj) return;
    jclass cls = env->GetObjectClass(gActivityObj);
    jmethodID mid = env->GetMethodID(cls, "nativeAudioPlay", "(Ljava/lang/String;)V");
    if (mid) {
        jstring jname = env->NewStringUTF(name);
        env->CallVoidMethod(gActivityObj, mid, jname);
        env->DeleteLocalRef(jname);
    }
    env->DeleteLocalRef(cls);
}

void androidStopAllSounds() {
    JNIEnv* env = getJniEnv();
    if (!env || !gActivityObj) return;
    jclass cls = env->GetObjectClass(gActivityObj);
    jmethodID mid = env->GetMethodID(cls, "nativeAudioStopAll", "()V");
    if (mid) env->CallVoidMethod(gActivityObj, mid);
    env->DeleteLocalRef(cls);
}

void androidSetVolume(float vol) {
    JNIEnv* env = getJniEnv();
    if (!env || !gActivityObj) return;
    jclass cls = env->GetObjectClass(gActivityObj);
    jmethodID mid = env->GetMethodID(cls, "nativeAudioSetVolume", "(F)V");
    if (mid) env->CallVoidMethod(gActivityObj, mid, vol);
    env->DeleteLocalRef(cls);
}

void androidPlayLoop(const char* name) {
    JNIEnv* env = getJniEnv();
    if (!env || !gActivityObj) return;
    jclass cls = env->GetObjectClass(gActivityObj);
    jmethodID mid = env->GetMethodID(cls, "nativeAudioPlayLoop", "(Ljava/lang/String;)V");
    if (mid) {
        jstring jname = env->NewStringUTF(name);
        env->CallVoidMethod(gActivityObj, mid, jname);
        env->DeleteLocalRef(jname);
    }
    env->DeleteLocalRef(cls);
}

void androidStopLoop() {
    JNIEnv* env = getJniEnv();
    if (!env || !gActivityObj) return;
    jclass cls = env->GetObjectClass(gActivityObj);
    jmethodID mid = env->GetMethodID(cls, "nativeAudioStopLoop", "()V");
    if (mid) env->CallVoidMethod(gActivityObj, mid);
    env->DeleteLocalRef(cls);
}
 
