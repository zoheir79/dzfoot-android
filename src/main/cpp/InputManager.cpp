#include "InputManager.h"
#include <cstring>
#include <cmath>

void InputManager::onTouchDown(float x, float y) {
    touchStartX_ = x;
    touchStartY_ = y;
    touching_ = true;
}

void InputManager::onTouchMove(float x, float y) {
    if (!touching_) return;
    float dx = x - touchStartX_;
    float dy = y - touchStartY_;
    // Normalize to [-1, 1]
    float len = dx * dx + dy * dy;
    if (len > 1.0f) {
        len = 1.0f / sqrtf(len);
        dx *= len;
        dy *= len;
    }
    input_.dir_x = dx;
    input_.dir_z = -dy; // screen Y is down, world Z is forward
}

void InputManager::onTouchUp() {
    touching_ = false;
    input_.dir_x = 0.0f;
    input_.dir_z = 0.0f;
}

void InputManager::serialize(uint8_t* out, size_t maxLen) const {
    if (maxLen < sizeof(PlayerInput)) return;
    std::memcpy(out, &input_, sizeof(PlayerInput));
}
 
