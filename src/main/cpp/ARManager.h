#pragma once
#include <jni.h>
#include "arcore_c_api.h"
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/asset_manager.h>

enum class CameraMode {
    AR,       // ARCore camera (device position)
    Classic   // Broadcast TV camera (follows ball + active player)
};

struct ARPose {
    float matrix[16];
    bool  valid;
};

class ARManager {
public:
    bool init(JNIEnv* env, jobject context, AAssetManager* assetMgr, bool skipArCore = false);
    void destroy();

    void onResume(JNIEnv* env, jobject context, jobject activity);
    void onPause();
    void onSurfaceCreated();
    void onDisplayGeometryChanged(int rotation, int width, int height);

    bool update();

    void getViewMatrix(float* out4x4) const;
    void getProjectionMatrix(float* out4x4, float near, float far) const;

    // Set the point the broadcast camera should track (scene coords).
    // Used to pan the TV camera along the pitch following the ball.
    void setCameraFocus(float sceneX, float sceneZ, float playerBiasX = 0.0f, float playerBiasZ = 0.0f, float ballSpeed = 0.0f) {
        focusX_ = sceneX;
        focusZ_ = sceneZ;
        playerBiasX_ = playerBiasX;
        playerBiasZ_ = playerBiasZ;
        ballSpeed_ = ballSpeed;
        // Smoothly interpolate camera focus with a 15% step for responsive ball tracking
        smoothFocusX_ = smoothFocusX_ * 0.85f + sceneX * 0.15f;
        smoothFocusZ_ = smoothFocusZ_ * 0.85f + sceneZ * 0.15f;
    }

    void setCameraMode(CameraMode mode) { camMode_ = mode; }
    CameraMode getCameraMode() const { return camMode_; }

    // Set actual scene pitch half-extents from renderer (metres)
    void setPitchExtents(float halfX, float halfZ) {
        sceneHalfX_ = halfX;
        sceneHalfZ_ = halfZ;
    }

    void setServerCamera(float x, float y, float z, float fov, const float* rot = nullptr) {
        hasServerCamera_ = true;
        serverCamX_ = x;
        serverCamY_ = y;
        serverCamZ_ = z;
        serverCamFov_ = fov;
        if (rot) {
            serverCamRot_[0] = rot[0];
            serverCamRot_[1] = rot[1];
            serverCamRot_[2] = rot[2];
            serverCamRot_[3] = rot[3];
            hasServerCameraRot_ = true;
        } else {
            hasServerCameraRot_ = false;
        }
    }
    void clearServerCamera() {
        hasServerCamera_ = false;
    }

    ARPose getMarkerAnchorPose() const;
    bool   isMarkerTracked()    const { if (!session_) return true; return markerTracked_; }

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

    // Broadcast camera focus target (scene coords), tracks the ball.
    float  focusX_            = 0.0f;
    float  focusZ_            = 0.0f;
    float  smoothFocusX_      = 0.0f;
    float  smoothFocusZ_      = 0.0f;

    // Player-biased tracking (0..1 weight for player vs ball)
    float  playerBiasX_       = 0.0f;
    float  playerBiasZ_       = 0.0f;
    float  ballSpeed_         = 0.0f;

    CameraMode camMode_ = CameraMode::Classic;

    bool  hasServerCamera_ = false;
    bool  hasServerCameraRot_ = false;
    float serverCamX_      = 0.0f;
    float serverCamY_      = 0.0f;
    float serverCamZ_      = 0.0f;
    float serverCamFov_     = 38.0f;
    float serverCamRot_[4]  = {0,0,0,1};

    // Actual scene pitch half-extents (in metres), set from renderer so the
    // broadcast camera clamps and positions itself proportionally.
    float sceneHalfX_ = 5.25f;
    float sceneHalfZ_ = 3.40f;

    // Camera shudder state (tremblement organique)
    mutable float shudderAccumX_ = 0.0f;
    mutable float shudderAccumY_ = 0.0f;
    mutable int   shudderSeed_   = 0;
};
