#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>

#include "ARManager.h"
#include "ARRenderer.h"
#include "GameBridge.h"
#include <cstring>

#define LOG_TAG "DZFootJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static ARManager gArManager;
static ARRenderer gRenderer;
static GameBridge gGameBridge;
static AAssetManager* gAssetManager = nullptr;

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_football_ar_JniBridge_nativeInit(JNIEnv* env, jobject thiz, jobject context, jobject assetManager) {
    gAssetManager = AAssetManager_fromJava(env, assetManager);
    if (!gArManager.init(env, context, gAssetManager)) {
        LOGI("ARManager init failed");
        return JNI_FALSE;
    }
    gRenderer.init();
    LOGI("Native init OK");
    return JNI_TRUE;
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
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeDisplayChanged(
    JNIEnv* env, jobject thiz, jint rotation, jint width, jint height) {
    gArManager.onDisplayGeometryChanged(rotation, width, height);
}

JNIEXPORT void JNICALL
Java_com_football_ar_JniBridge_nativeOnFrame(
    JNIEnv* env, jobject thiz,
    jfloatArray viewMat, jfloatArray projMat, jfloatArray anchorMat,
    jbyteArray gameStateData) {

    if (!gArManager.update()) return;

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

    // Apply game state if present
    if (gameStateData) {
        jbyte* state = env->GetByteArrayElements(gameStateData, nullptr);
        gGameBridge.applyGameState((const uint8_t*)state, env->GetArrayLength(gameStateData));
        env->ReleaseByteArrayElements(gameStateData, state, JNI_ABORT);
    }

    // Render camera background
    gRenderer.drawCameraBackground(gArManager);

    // Render game on marker if tracked
    if (gArManager.isMarkerTracked()) {
        const GameState& gs = gGameBridge.currentState();
        float positions[66]; // 22 players * 3
        for (int i = 0; i < 22; ++i) {
            positions[i * 3 + 0] = gs.players[i].pos[0];
            positions[i * 3 + 1] = gs.players[i].pos[1];
            positions[i * 3 + 2] = gs.players[i].pos[2];
        }
        gRenderer.renderGameOnMarker(gArManager, positions, 22);
    }
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

} // extern "C"
