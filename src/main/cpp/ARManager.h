#pragma once
#include <jni.h>
#include "arcore_c_api.h"
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/asset_manager.h>

struct ARPose {
    float matrix[16];
    bool  valid;
};

class ARManager {
public:
    bool init(JNIEnv* env, jobject context, AAssetManager* assetMgr);
    void destroy();

    void onResume(JNIEnv* env, jobject context, jobject activity);
    void onPause();
    void onSurfaceCreated();
    void onDisplayGeometryChanged(int rotation, int width, int height);

    bool update();

    void getViewMatrix(float* out4x4) const;
    void getProjectionMatrix(float* out4x4, float near, float far) const;

    ARPose getMarkerAnchorPose() const;
    bool   isMarkerTracked()    const { return markerTracked_; }

    GLuint getCameraTextureId() const { return cameraTextureId_; }

    int displayWidth()  const { return displayWidth_; }
    int displayHeight() const { return displayHeight_; }

private:
    void setupMarkerDetection(JNIEnv* env, AAssetManager* assetMgr);
    void checkForMarker();

    ArSession* session_       = nullptr;
    ArFrame*   frame_         = nullptr;
    ArCamera*  camera_        = nullptr;
    ArAnchor*  markerAnchor_  = nullptr;

    GLuint cameraTextureId_   = 0;
    bool   markerTracked_     = false;
    int    displayWidth_      = 0;
    int    displayHeight_     = 0;
    int    displayRotation_   = 0;
};
