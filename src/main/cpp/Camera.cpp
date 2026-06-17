#include "Camera.h"
#include <cmath>

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

static float length3(float x, float y, float z) {
    return std::sqrt(x*x + y*y + z*z);
}

static void normalize3(float& x, float& y, float& z) {
    float len = length3(x,y,z);
    if (len > 0.0001f) { x/=len; y/=len; z/=len; }
}

void Camera::lookAt(float ex, float ey, float ez,
                    float cx, float cy, float cz,
                    float ux, float uy, float uz) {
    pos_[0] = ex; pos_[1] = ey; pos_[2] = ez;
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    normalize3(fx, fy, fz);
    fwd_[0] = fx; fwd_[1] = fy; fwd_[2] = fz;

    float sx = fy*uz - fz*uy;
    float sy = fz*ux - fx*uz;
    float sz = fx*uy - fy*ux;
    normalize3(sx, sy, sz);
    right_[0] = sx; right_[1] = sy; right_[2] = sz;

    float ux2 = sy*fz - sz*fy;
    float uy2 = sz*fx - sx*fz;
    float uz2 = sx*fy - sy*fx;
    up_[0] = ux2; up_[1] = uy2; up_[2] = uz2;

    view_[0] = sx;   view_[1] = ux2;  view_[2] = -fx;  view_[3] = 0;
    view_[4] = sy;   view_[5] = uy2;  view_[6] = -fy;  view_[7] = 0;
    view_[8] = sz;   view_[9] = uz2;  view_[10]= -fz;  view_[11]= 0;
    view_[12]= -(sx*ex + sy*ey + sz*ez);
    view_[13]= -(ux2*ex + uy2*ey + uz2*ez);
    view_[14]= fx*ex + fy*ey + fz*ez;
    view_[15]= 1;
}

void Camera::perspective(float fovY, float aspect, float near, float far) {
    float f = 1.0f / std::tan(fovY * 0.5f);
    float nf = 1.0f / (near - far);
    for (int i = 0; i < 16; ++i) proj_[i] = 0.0f;
    proj_[0] = f / aspect;
    proj_[5] = f;
    proj_[10] = (far + near) * nf;
    proj_[11] = -1.0f;
    proj_[14] = 2.0f * far * near * nf;
}

void Camera::getViewProj(float* out) const {
    mat4Mul(proj_, view_, out);
}

void Camera::setPosition(float x, float y, float z) {
    pos_[0] = x; pos_[1] = y; pos_[2] = z;
    lookAt(pos_[0], pos_[1], pos_[2],
           pos_[0]+fwd_[0], pos_[1]+fwd_[1], pos_[2]+fwd_[2],
           up_[0], up_[1], up_[2]);
}

void Camera::moveForward(float dist) {
    pos_[0] += fwd_[0] * dist;
    pos_[1] += fwd_[1] * dist;
    pos_[2] += fwd_[2] * dist;
    setPosition(pos_[0], pos_[1], pos_[2]);
}

void Camera::moveRight(float dist) {
    pos_[0] += right_[0] * dist;
    pos_[1] += right_[1] * dist;
    pos_[2] += right_[2] * dist;
    setPosition(pos_[0], pos_[1], pos_[2]);
}

void Camera::rotate(float yaw, float pitch) {
    // yaw around up, pitch around right
    float cy = std::cos(yaw), sy = std::sin(yaw);
    float cp = std::cos(pitch), sp = std::sin(pitch);

    float nx = fwd_[0]*cy + fwd_[2]*sy;
    float nz = -fwd_[0]*sy + fwd_[2]*cy;
    fwd_[0] = nx; fwd_[2] = nz;
    normalize3(fwd_[0], fwd_[1], fwd_[2]);

    float ny = fwd_[1]*cp - fwd_[2]*sp;
    nz = fwd_[1]*sp + fwd_[2]*cp;
    fwd_[1] = ny; fwd_[2] = nz;
    normalize3(fwd_[0], fwd_[1], fwd_[2]);

    setPosition(pos_[0], pos_[1], pos_[2]);
}
