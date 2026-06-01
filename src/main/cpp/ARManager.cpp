#include "ARManager.h"
#include <android/log.h>
#include <android/bitmap.h>
#include <vector>
#define LOG_TAG "ARManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

bool ARManager::init(JNIEnv* env, jobject ctx, AAssetManager* assetMgr, bool skipArCore) {
    if (skipArCore) {
        LOGI("Emulator mode: skipping ARCore session creation entirely");
        session_ = nullptr;
        return true; // Fallback: continue without AR
    }

    ArStatus status = ArSession_create(env, ctx, &session_);
    if (status != AR_SUCCESS) {
        LOGI("ARCore session create failed: %d, running in fallback mode", status);
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

    LOGI("ARCore init OK");
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
        LOGI("marker_dzfoot.png not found in assets!");
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
        LOGI("Failed to decode marker.jpg via BitmapFactory");
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
        LOGI("Failed to add image to DB: %d", status);
        ArAugmentedImageDatabase_destroy(db);
        return;
    }

    ArConfig* cfg;
    ArConfig_create(session_, &cfg);
    ArConfig_setAugmentedImageDatabase(session_, cfg, db);
    ArSession_configure(session_, cfg);
    ArConfig_destroy(cfg);
    ArAugmentedImageDatabase_destroy(db);
    LOGI("Marker detection configured (%dx%d, index %d)", width, height, idx);
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
                LOGI("Marker anchor created!");
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
    m[2]=-f[0]; m[6]=-f[1]; m[10]=-f[2];m[14]=(f[0]*eyeX+f[1]*eyeY+f[2]*eyeZ);
    m[3]=0;     m[7]=0;     m[11]=0;     m[15]=1;
}

void ARManager::getViewMatrix(float* out) const {
    if (camera_) {
        ArCamera_getViewMatrix(session_, camera_, out);
    } else {
        // Fallback camera: close TV broadcast camera (FIFA style)
        lookAt(out, 0.0f, 2.5f, 3.5f,     // eye: lower and closer to the pitch
                     0.0f, 0.0f, -0.2f,   // center: midfield
                     0.0f, 1.0f, 0.0f);   // up: Y is up
    }
}

void ARManager::getProjectionMatrix(float* out, float near, float far) const {
    if (camera_) {
        ArCamera_getProjectionMatrix(session_, camera_, near, far, out);
    } else {
        // Simple perspective for fallback (use actual display aspect)
        // Column-major OpenGL projection matrix
        float fov = 60.0f * 3.14159f / 180.0f;
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
 
