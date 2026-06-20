#include "ARManager.h"
#include "SceneGraph.h"
#include <android/log.h>
#include <android/bitmap.h>
#include <vector>
#define LOG_TAG "ARManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

bool ARManager::init(JNIEnv* env, jobject ctx, AAssetManager* assetMgr, bool skipArCore) {
    // Clean up any previous session resources to prevent stale pointers
    // when init() is called multiple times (e.g. onCreate + checkArCoreAndInit).
    if (markerAnchor_) { ArAnchor_release(markerAnchor_); markerAnchor_ = nullptr; }
    if (camera_) { ArCamera_release(camera_); camera_ = nullptr; }
    if (frame_) { ArFrame_destroy(frame_); frame_ = nullptr; }
    if (session_) {
        ArSession_pause(session_);
        ArSession_destroy(session_);
        session_ = nullptr;
    }
    hasServerCamera_ = false;

    if (skipArCore) {
        session_ = nullptr;
        return true; // Fallback: continue without AR
    }

    ArStatus status = ArSession_create(env, ctx, &session_);
    if (status != AR_SUCCESS) {
        session_ = nullptr;
        return true; // Fallback: continue without AR
    }

    ArConfig* config;
    ArConfig_create(session_, &config);
    ArConfig_setFocusMode(session_, config, AR_FOCUS_MODE_AUTO);
    ArSession_configure(session_, config);
    ArConfig_destroy(config);
    setupMarkerDetection(env, assetMgr);

    glGenTextures(1, &cameraTextureId_);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, cameraTextureId_);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ArSession_setCameraTextureName(session_, cameraTextureId_);

    return true;
}

void ARManager::destroy() {
    if (markerAnchor_) { ArAnchor_release(markerAnchor_); markerAnchor_ = nullptr; }
    if (camera_) { ArCamera_release(camera_); camera_ = nullptr; }
    if (frame_) { ArFrame_destroy(frame_); frame_ = nullptr; }
    if (session_) {
        ArSession_pause(session_);
        ArSession_destroy(session_);
        session_ = nullptr;
    }
    if (cameraTextureId_) { glDeleteTextures(1, &cameraTextureId_); cameraTextureId_ = 0; }
}

void ARManager::setupMarkerDetection(JNIEnv* env, AAssetManager* assetMgr) {
    AAsset* asset = AAssetManager_open(assetMgr, "marker_dzfoot.png", AASSET_MODE_BUFFER);
    if (!asset) {
        return;
    }
    const uint8_t* imgData = (const uint8_t*)AAsset_getBuffer(asset);
    int64_t imgSize = AAsset_getLength(asset);

    // 1. Decode JPEG using Java BitmapFactory
    jclass factoryClass = env->FindClass("android/graphics/BitmapFactory");
    jmethodID decodeMethod = env->GetStaticMethodID(factoryClass, "decodeByteArray", "([BII)Landroid/graphics/Bitmap;");

    jbyteArray array = env->NewByteArray((jsize)imgSize);
    env->SetByteArrayRegion(array, 0, (jsize)imgSize, (const jbyte*)imgData);

    jobject bitmap = env->CallStaticObjectMethod(factoryClass, decodeMethod, array, 0, (jint)imgSize);
    AAsset_close(asset);

    if (!bitmap) {
        return;
    }

    // 2. Get pixels and convert to grayscale
    AndroidBitmapInfo info;
    AndroidBitmap_getInfo(env, bitmap, &info);

    void* pixels;
    AndroidBitmap_lockPixels(env, bitmap, &pixels);

    int32_t width = (int32_t)info.width;
    int32_t height = (int32_t)info.height;
    int32_t stride = width; // Grayscale stride

    std::vector<uint8_t> grayscale(width * height);
    uint32_t* rgbaPixels = (uint32_t*)pixels;
    for (int i = 0; i < width * height; ++i) {
        uint32_t p = rgbaPixels[i];
        // BitmapFactory usually returns ARGB_8888, which is 0xAABBGGRR on little-endian
        uint8_t r = (uint8_t)(p & 0xFF);
        uint8_t g = (uint8_t)((p >> 8) & 0xFF);
        uint8_t b = (uint8_t)((p >> 16) & 0xFF);
        grayscale[i] = (uint8_t)(0.299f * (float)r + 0.587f * (float)g + 0.114f * (float)b);
    }
    AndroidBitmap_unlockPixels(env, bitmap);

    // 3. Add to ARCore database
    ArAugmentedImageDatabase* db;
    ArAugmentedImageDatabase_create(session_, &db);
    int32_t idx;
    ArStatus status = ArAugmentedImageDatabase_addImageWithPhysicalSize(
        session_, db, "football_marker", grayscale.data(), width, height, stride, 0.21f, &idx);

    if (status != AR_SUCCESS) {
        ArAugmentedImageDatabase_destroy(db);
        return;
    }

    ArConfig* cfg;
    ArConfig_create(session_, &cfg);
    ArConfig_setAugmentedImageDatabase(session_, cfg, db);
    ArSession_configure(session_, cfg);
    ArConfig_destroy(cfg);
    ArAugmentedImageDatabase_destroy(db);
}

bool ARManager::update() {
    if (!session_) {
        markerTracked_ = true; // Force render in fallback
        return true;
    }
    if (frame_) ArFrame_destroy(frame_);
    ArFrame_create(session_, &frame_);

    ArStatus status = ArSession_update(session_, frame_);
    if (status != AR_SUCCESS) {
        // Don't bail entirely; allow render of game with fallback view (useful on emulator)
        return true;
    }

    if (camera_) ArCamera_release(camera_);
    ArFrame_acquireCamera(session_, frame_, &camera_);

    ArTrackingState trackingState;
    ArCamera_getTrackingState(session_, camera_, &trackingState);
    if (trackingState != AR_TRACKING_STATE_TRACKING) {
        // AR tracking lost (common on emulator) - keep rendering anyway
        return true;
    }

    checkForMarker();
    return true;
}

void ARManager::checkForMarker() {
    ArTrackableList* list;
    ArTrackableList_create(session_, &list);
    ArFrame_getUpdatedTrackables(session_, frame_, AR_TRACKABLE_AUGMENTED_IMAGE, list);

    int32_t size;
    ArTrackableList_getSize(session_, list, &size);
    for (int i = 0; i < size; ++i) {
        ArTrackable* trackable;
        ArTrackableList_acquireItem(session_, list, i, &trackable);
        ArAugmentedImage* img = ArAsAugmentedImage(trackable);
        ArTrackingState state;
        ArTrackable_getTrackingState(session_, trackable, &state);

        if (state == AR_TRACKING_STATE_TRACKING) {
            if (!markerAnchor_) {
                ArPose* pose;
                ArPose_create(session_, nullptr, &pose);
                ArAugmentedImage_getCenterPose(session_, img, pose);
                ArTrackable_acquireNewAnchor(session_, trackable, pose, &markerAnchor_);
                ArPose_destroy(pose);
            }
            markerTracked_ = true;
        } else {
            markerTracked_ = false;
        }
        ArTrackable_release(trackable);
    }
    ArTrackableList_destroy(list);
}

// Helper: normalize vector
static void normalize(float* v) {
    float len = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (len > 0.0001f) { v[0]/=len; v[1]/=len; v[2]/=len; }
}
// Helper: build LookAt matrix (right-handed, column-major for OpenGL)
static void lookAt(float* m, float eyeX, float eyeY, float eyeZ,
                   float centerX, float centerY, float centerZ,
                   float upX, float upY, float upZ) {
    float f[3] = {centerX-eyeX, centerY-eyeY, centerZ-eyeZ};
    normalize(f);
    float up[3] = {upX, upY, upZ};
    normalize(up);
    float s[3] = {f[1]*up[2]-f[2]*up[1], f[2]*up[0]-f[0]*up[2], f[0]*up[1]-f[1]*up[0]};
    normalize(s);
    float u[3] = {s[1]*f[2]-s[2]*f[1], s[2]*f[0]-s[0]*f[2], s[0]*f[1]-s[1]*f[0]};

    // Column-major order for OpenGL
    m[0]=s[0];  m[4]=s[1];  m[8]=s[2];  m[12]=-(s[0]*eyeX+s[1]*eyeY+s[2]*eyeZ);
    m[1]=u[0];  m[5]=u[1];  m[9]=u[2];  m[13]=-(u[0]*eyeX+u[1]*eyeY+u[2]*eyeZ);
    m[2]=-f[0]; m[6]=-f[1]; m[10]=-f[2];m[14]=f[0]*eyeX+f[1]*eyeY+f[2]*eyeZ;
    m[3]=0;     m[7]=0;     m[11]=0;     m[15]=1;
}

void ARManager::getViewMatrix(float* out) const {
    // 1. If in AR mode and we have an active ARCore session + camera, use ARCore camera matrix
    if (session_ && camera_ && camMode_ == CameraMode::AR) {
        ArCamera_getViewMatrix(session_, camera_, out);
        return;
    }

    // 2. Exact GF camera via quaternion (position + rotation)
    if (hasServerCamera_ && hasServerCameraRot_) {
        // GF uses Z-up (X=length, Y=width, Z=height).
        // OpenGL uses Y-up (X=length, Y=height, Z=width).
        // Right-handed basis change: (X,Y,Z)_gf -> (X,Z,-Y)_ogl.
        // For a quaternion q=(x,y,z,w), the converted quaternion is q_ogl=(x,-z,-y,w).
        float qOgl[4] = { serverCamRot_[0], -serverCamRot_[2], -serverCamRot_[1], serverCamRot_[3] };
        float rot[16];
        Transform::quatToMat4(qOgl, rot);
        // View matrix = transpose (inverse of orthonormal rotation)
        out[0] = rot[0];   out[4] = rot[1];   out[8]  = rot[2];   out[12] = -(rot[0]*serverCamX_ + rot[1]*serverCamY_ + rot[2]*serverCamZ_);
        out[1] = rot[4];   out[5] = rot[5];   out[9]  = rot[6];   out[13] = -(rot[4]*serverCamX_ + rot[5]*serverCamY_ + rot[6]*serverCamZ_);
        out[2] = rot[8];   out[6] = rot[9];   out[10] = rot[10];  out[14] = -(rot[8]*serverCamX_ + rot[9]*serverCamY_ + rot[10]*serverCamZ_);
        out[3] = 0;        out[7] = 0;        out[11] = 0;        out[15] = 1;

        static int gfCamLogCounter = 0;
        if ((gfCamLogCounter++ % 120) == 0) {
            LOGI("[GF_CAM] pos=(%.2f,%.2f,%.2f) fov=%.1f->%.1f rot=(%.3f,%.3f,%.3f,%.3f)",
                 serverCamX_, serverCamY_, serverCamZ_,
                 serverCamFov_, serverCamFov_ * 1.5f,
                 serverCamRot_[0], serverCamRot_[1], serverCamRot_[2], serverCamRot_[3]);
        }
        return;
    }

    // 3. Otherwise (emulator/fallback OR Classic mode on real phone with cleared server camera),
    //    always use the fully dynamic, proportional Broadcast TV Camera!
    // Classic Broadcast TV Camera — exact match to GameplayFootball PC
    // Source: match.cpp UpdateIngameCamera() wide cam (camMethod == 1)
    // Default settings: zoom=0.5 height=0.3 fov=0.4 anglefactor=0.0

    // 1. Target = ball*(1-bias) + designatedPlayer*bias  (PC uses 0.6 bias)
    const float playerBias = 0.6f;
    const float ballWeight = 1.0f - playerBias;
    float targetX = smoothFocusX_ * ballWeight + playerBiasX_ * playerBias;
    float targetZ = smoothFocusZ_ * ballWeight + playerBiasZ_ * playerBias;

    // Clamp target to pitch bounds (same ratios as PC: 0.84W, 0.60H)
    const float maxW = sceneHalfX_ * 0.84f;
    const float maxH = sceneHalfZ_ * 0.60f;
    if (fabsf(targetX) > maxW) targetX = maxW * (targetX > 0 ? 1.0f : -1.0f);
    if (fabsf(targetZ) > maxH) targetZ = maxH * (targetZ > 0 ? 1.0f : -1.0f);

    // 2. Organic shudder — reduced for steady broadcast feel
    shudderSeed_++;
    float shudderAmt = (ballSpeed_ * 0.8f + 2.0f) * 0.02f;
    float noiseX = (float)(shudderSeed_ % 17 - 8) / 8.5f;
    float noiseY = (float)((shudderSeed_ * 7) % 13 - 6) / 6.5f;
    shudderAccumX_ = shudderAccumX_ * 0.94f + noiseX * shudderAmt * 0.06f;
    shudderAccumY_ = shudderAccumY_ * 0.94f + noiseY * shudderAmt * 0.06f;

    // 3. Camera position — sideline broadcast TV framing
    // Close enough for player detail, low angle for immersive side-view
    float camDistZ = sceneHalfZ_ * 0.65f;
    float camHeight = sceneHalfZ_ * 0.18f;
    float camX = targetX * 0.85f + shudderAccumX_;
    float camZ = targetZ * 0.75f + camDistZ; // +Z side in right-handed world
    float camY = camHeight + shudderAccumY_;

    static int camLogCounter = 0;
    if ((camLogCounter++ % 60) == 0) {
        LOGI("[CAMERA] sceneHalf=(%.2f,%.2f) target=(%.2f,%.2f) cam=(%.2f,%.2f,%.2f) distZ=%.2f height=%.2f",
             sceneHalfX_, sceneHalfZ_, targetX, targetZ, camX, camY, camZ, camDistZ, camHeight);
    }

    // Look at player head height for horizontal sideline broadcast feel
    float targetY = 1.60f;
    lookAt(out, camX, camY, camZ,
                 targetX, targetY, targetZ,
                 0.0f, 1.0f, 0.0f);
}

void ARManager::getProjectionMatrix(float* out, float near, float far) const {
    if (camera_ && camMode_ == CameraMode::AR) {
        ArCamera_getProjectionMatrix(session_, camera_, near, far, out);
        return;
    }
    float fovDeg = 30.0f;
    // Use server camera FOV when available (emulator or real device)
    if (hasServerCamera_) {
        // GF desktop FOV (~20°) is designed for large monitors. On mobile screens
        // the same FOV shows only ~34% of the pitch — too zoomed in. Apply a 1.5x
        // mobile multiplier and clamp to [30°, 65°] so dynamic GF FOV changes
        // (goal celebrations ~35°, replays ~56°) still behave naturally.
        fovDeg = serverCamFov_ * 1.5f;
        if (fovDeg < 30.0f) fovDeg = 30.0f;
        if (fovDeg > 65.0f) fovDeg = 65.0f;
    } else {
        // Broadcast telephoto — moderate FOV for player detail plus tactical context
        //    30° midfield → 34° near sideline for consistent zoom feel
        float distRatio = std::abs(smoothFocusX_) / 5.25f; // 0=center, 1=sideline
        fovDeg = 30.0f + distRatio * 4.0f; // 30° → 34° telephoto
    }
    float fov = fovDeg * 3.14159265f / 180.0f;
    float f = 1.0f / tanf(fov / 2.0f);
    float aspect = (displayHeight_ > 0) ? (float)displayWidth_ / (float)displayHeight_ : 16.0f / 9.0f;
    // Column-major layout: each group of 4 floats is one COLUMN
    float persp[16] = {
        f/aspect, 0,    0,                          0,    // col 0
        0,        f,    0,                          0,    // col 1
        0,        0,    (far+near)/(near-far),      -1,   // col 2
        0,        0,    (2*far*near)/(near-far),    0     // col 3
    };
    memcpy(out, persp, 16*sizeof(float));
}

ARPose ARManager::getMarkerAnchorPose() const {
    ARPose pose;
    pose.valid = false;
    if (!session_ || !markerAnchor_) return pose;

    ArTrackingState state;
    ArAnchor_getTrackingState(session_, markerAnchor_, &state);
    if (state != AR_TRACKING_STATE_TRACKING) return pose;

    ArPose* arpose;
    ArPose_create(session_, nullptr, &arpose);
    ArAnchor_getPose(session_, markerAnchor_, arpose);
    ArPose_getMatrix(session_, arpose, pose.matrix);
    ArPose_destroy(arpose);
    pose.valid = true;
    return pose;
}

void ARManager::onResume(JNIEnv* env, jobject ctx, jobject activity) {
    if (session_) ArSession_resume(session_);
}
void ARManager::onPause() {
    if (session_) ArSession_pause(session_);
}
void ARManager::onDisplayGeometryChanged(int rot, int w, int h) {
    displayRotation_ = rot;
    displayWidth_ = w;
    displayHeight_ = h;
    if (session_) ArSession_setDisplayGeometry(session_, rot, w, h);
}
void ARManager::onSurfaceCreated() {}
 
