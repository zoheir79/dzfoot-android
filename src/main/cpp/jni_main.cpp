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
#include "InputManager.h"
#include "AnimationPlayer.h"
#include "AssetLoader.h"
#include "protocol/DZFootProtocol.h"
#include <cstring>

#define LOG_TAG "DZFootJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static constexpr const char* NATIVE_BUILD_MARKER = "DZFOOT_DIR3_2026_06_02_0126";

// Forward declaration of protocol test (tests/test_protocol_layout.cpp)
extern bool runProtocolTests();

static ARManager gArManager;
static ARRenderer gRenderer;
static GameBridge gGameBridge;
static InputManager gInputManager;
AnimationPlayer gAnimPlayer;
AAssetManager* gAssetManager = nullptr;
static bool gRendererInited = false;
static bool gAnimLoaded = false;
jobject gActivityObj = nullptr;
JavaVM* gJavaVM = nullptr;

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
    glViewport(0, 0, width, height);
    LOGI("Viewport set: %dx%d rotation=%d", width, height, rotation);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeOnFrame(
    JNIEnv* env, jobject thiz,
    jfloatArray viewMat, jfloatArray projMat, jfloatArray anchorMat,
    jbyteArray gameStateData) {

    if (!gArManager.update()) return;

    // Diagnostic: confirm nativeOnFrame is executing
    static int frameNum = 0;
    if (frameNum++ % 120 == 0) {
        LOGI("%s frame=%d gsLen=%d", NATIVE_BUILD_MARKER, frameNum,
             gameStateData ? env->GetArrayLength(gameStateData) : -1);
    }

    // Clear screen (vivid stadium sky blue gradient)
    glClearColor(0.45f, 0.72f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Get AR matrices
    jfloat* view = env->GetFloatArrayElements(viewMat, nullptr);
    jfloat* proj = env->GetFloatArrayElements(projMat, nullptr);
    jfloat* anchor = env->GetFloatArrayElements(anchorMat, nullptr);
    gArManager.getViewMatrix(view);
    gArManager.getProjectionMatrix(proj, 0.01f, 100.0f);
    ARPose pose = gArManager.getMarkerAnchorPose();
    if (pose.valid) {
        std::memcpy(anchor, pose.matrix, 16 * sizeof(float));
    }
    env->ReleaseFloatArrayElements(viewMat, view, 0);
    env->ReleaseFloatArrayElements(projMat, proj, 0);
    env->ReleaseFloatArrayElements(anchorMat, anchor, 0);

    // Apply game state if present and not empty
    if (gameStateData && env->GetArrayLength(gameStateData) > 0) {
        jbyte* state = env->GetByteArrayElements(gameStateData, nullptr);
        gGameBridge.applyGameState((const uint8_t*)state, env->GetArrayLength(gameStateData));
        env->ReleaseByteArrayElements(gameStateData, state, JNI_ABORT);
    }

    // Render camera background
    gRenderer.drawCameraBackground(gArManager);

    // Always render game (use fallback view if marker not tracked)
    // Use interpolated state for smooth 20 Hz display
    dzfoot::GameStatePacket gs = gGameBridge.getInterpolatedState();

    // If no remote game state, apply local input (offline mode)
    if (!gameStateData || env->GetArrayLength(gameStateData) == 0) {
        const dzfoot::PlayerInputPacket& input = gInputManager.getInput();
        float speed = 0.05f;
        gs.players[0].pos[0] += input.dirX * speed;
        gs.players[0].pos[1] += input.dirZ * speed;
        gs.players[0].vel[0] = input.dirX * speed;
        gs.players[0].vel[1] = input.dirZ * speed;
        gs.players[0].vel[2] = 0.0f;
        if (std::fabs(input.dirX) > 0.001f || std::fabs(input.dirZ) > 0.001f) {
            gs.players[0].rotY = std::atan2(input.dirX, input.dirZ);
            gs.players[0].anim = input.buttons & dzfoot::BUTTON_SPRINT ? dzfoot::ANIM_SPRINT : dzfoot::ANIM_WALK;
        } else {
            gs.players[0].anim = dzfoot::ANIM_IDLE;
        }
        if (gs.players[0].pos[0] < -1.0f) gs.players[0].pos[0] = -1.0f;
        if (gs.players[0].pos[0] > 1.0f) gs.players[0].pos[0] = 1.0f;
        if (gs.players[0].pos[1] < -0.43f) gs.players[0].pos[1] = -0.43f;
        if (gs.players[0].pos[1] > 0.43f) gs.players[0].pos[1] = 0.43f;
        gs.ball.pos[0] = gs.players[0].pos[0] + 0.15f;
        gs.ball.pos[1] = gs.players[0].pos[1];
        gs.ball.pos[2] = 0.25f;
    }

    float positions[66]; // 22 players * 3
    uint8_t playerAnims[22];
    float playerVels[66];
    float playerRotY[22];
    uint8_t playerFlags[22];
    uint8_t playerTeams[22];
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
    }
    float ballPos[3] = { gs.ball.pos[0], gs.ball.pos[1], gs.ball.pos[2] };
    gRenderer.renderScene(gArManager, positions, 22, ballPos,
                          nullptr, 0, playerAnims, playerVels, playerRotY,
                          playerFlags, playerTeams);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeOnTouch(
    JNIEnv* env, jobject thiz, jfloat x, jfloat y, jint action) {
    if (action == 0) { // ACTION_DOWN
        gInputManager.onTouchDown(x, y);
    } else if (action == 2) { // ACTION_MOVE
        gInputManager.onTouchMove(x, y);
    } else if (action == 1 || action == 3) { // ACTION_UP or ACTION_CANCEL
        gInputManager.onTouchUp();
    }
}

JNIEXPORT jbyteArray JNICALL
Java_com_football_ar_JniBridge_nativeGetInputBytes(JNIEnv* env, jobject thiz) {
    constexpr size_t pktSize = sizeof(dzfoot::PlayerInputPacket);
    uint8_t buf[pktSize];
    gInputManager.serialize(buf, pktSize);
    jbyteArray result = env->NewByteArray(static_cast<jsize>(pktSize));
    env->SetByteArrayRegion(result, 0, static_cast<jsize>(pktSize), (jbyte*)buf);
    return result;
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeSetActionKick(JNIEnv* env, jobject thiz, jboolean on) {
    gInputManager.setActionKick(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeSetActionPass(JNIEnv* env, jobject thiz, jboolean on) {
    gInputManager.setActionPass(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeSetActionShot(JNIEnv* env, jobject thiz, jboolean on) {
    gInputManager.setActionShot(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeSetActionDribble(JNIEnv* env, jobject thiz, jboolean on) {
    gInputManager.setActionDribble(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeSetSprint(JNIEnv* env, jobject thiz, jboolean on) {
    gInputManager.setSprint(on == JNI_TRUE);
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
 
