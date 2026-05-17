#include "ARManager.h"
#include <android/log.h>
#define LOG_TAG "ARManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

bool ARManager::init(JNIEnv* env, jobject ctx, AAssetManager* assetMgr) {
    ArStatus status = ArSession_create(env, ctx, &session_);
    if (status != AR_SUCCESS) {
        LOGI("ARCore session create failed: %d", status);
        return false;
    }

    ArConfig* config;
    ArConfig_create(session_, &config);
    ArConfig_setFocusMode(session_, config, AR_FOCUS_MODE_AUTO);
    setupMarkerDetection(assetMgr);
    ArSession_configure(session_, config);
    ArConfig_destroy(config);

    glGenTextures(1, &cameraTextureId_);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, cameraTextureId_);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ArSession_setCameraTextureName(session_, cameraTextureId_);

    LOGI("ARCore init OK");
    return true;
}

void ARManager::destroy() {
    if (markerAnchor_) ArAnchor_release(markerAnchor_);
    if (camera_) ArCamera_release(camera_);
    if (frame_) ArFrame_destroy(frame_);
    if (session_) {
        ArSession_pause(session_);
        ArSession_destroy(session_);
    }
    if (cameraTextureId_) glDeleteTextures(1, &cameraTextureId_);
}

void ARManager::setupMarkerDetection(AAssetManager* assetMgr) {
    AAsset* asset = AAssetManager_open(assetMgr, "marker.jpg", AASSET_MODE_BUFFER);
    if (!asset) {
        LOGI("marker.jpg not found in assets!");
        return;
    }
    const uint8_t* imgData = (const uint8_t*)AAsset_getBuffer(asset);
    int64_t imgSize = AAsset_getLength(asset);

    ArAugmentedImageDatabase* db;
    ArAugmentedImageDatabase_create(session_, &db);
    int32_t idx;
    ArAugmentedImageDatabase_addImageWithPhysicalSize(
        session_, db, "football_marker", imgData, imgSize, 0.21f, &idx);
    AAsset_close(asset);

    ArConfig* cfg;
    ArConfig_create(session_, &cfg);
    ArConfig_setAugmentedImageDatabase(session_, cfg, db);
    ArSession_configure(session_, cfg);
    ArConfig_destroy(cfg);
    ArAugmentedImageDatabase_destroy(db);
    LOGI("Marker detection configured (index %d)", idx);
}

bool ARManager::update() {
    if (!session_) return false;
    if (frame_) ArFrame_destroy(frame_);
    ArFrame_create(session_, &frame_);

    ArStatus status = ArSession_update(session_, frame_);
    if (status != AR_SUCCESS) return false;

    if (camera_) ArCamera_release(camera_);
    ArFrame_acquireCamera(session_, frame_, &camera_);

    ArTrackingState trackingState;
    ArCamera_getTrackingState(session_, camera_, &trackingState);
    if (trackingState != AR_TRACKING_STATE_TRACKING) return false;

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
                ArAugmentedImage_acquireNewAnchor(session_, img, &markerAnchor_);
                LOGI("Marker anchor created!");
            }
            markerTracked_ = true;
        }
        ArTrackable_release(trackable);
    }
    ArTrackableList_destroy(list);
}

void ARManager::getViewMatrix(float* out) const {
    if (camera_) ArCamera_getViewMatrix(session_, camera_, out);
}

void ARManager::getProjectionMatrix(float* out, float near, float far) const {
    if (camera_) ArCamera_getProjectionMatrix(session_, camera_, near, far, out);
}

ARPose ARManager::getMarkerAnchorPose() const {
    ARPose pose;
    pose.valid = false;
    if (!markerAnchor_) return pose;

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
