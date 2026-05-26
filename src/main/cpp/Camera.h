#pragma once

class Camera {
public:
    void lookAt(float eyeX, float eyeY, float eyeZ,
                float centerX, float centerY, float centerZ,
                float upX, float upY, float upZ);

    void perspective(float fovY, float aspect, float near, float far);

    const float* viewMatrix() const { return view_; }
    const float* projMatrix() const { return proj_; }

    void getViewProj(float* out) const;

    void setPosition(float x, float y, float z);
    void moveForward(float dist);
    void moveRight(float dist);
    void rotate(float yaw, float pitch);

private:
    float view_[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float proj_[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float pos_[3] = {0,0,0};
    float fwd_[3] = {0,0,-1};
    float right_[3] = {1,0,0};
    float up_[3] = {0,1,0};
};
