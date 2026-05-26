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
    
    // Standard virtual joystick radius in pixels
    constexpr float JOYSTICK_RADIUS = 100.0f;
    float len = sqrtf(dx * dx + dy * dy);
    
    if (len > 0.001f) {
        float factor = (len > JOYSTICK_RADIUS) ? 1.0f : (len / JOYSTICK_RADIUS);
        dx = (dx / len) * factor;
        dy = (dy / len) * factor;
    } else {
        dx = 0.0f;
        dy = 0.0f;
    }
    
    input_.dirX = dx;
    input_.dirZ = -dy; // screen Y is down, world Z is forward
}

void InputManager::onTouchUp() {
    touching_ = false;
    input_.dirX = 0.0f;
    input_.dirZ = 0.0f;
}

void InputManager::serialize(uint8_t* out, size_t maxLen) const {
    if (maxLen < sizeof(dzfoot::PlayerInputPacket)) return;
    dzfoot::PlayerInputPacket pkt = input_;
    pkt.header.magic   = dzfoot::DZ_MAGIC;
    pkt.header.version = dzfoot::DZ_PROTOCOL_VERSION;
    pkt.header.type    = dzfoot::PACKET_PLAYER_INPUT;
    pkt.header.size    = static_cast<uint16_t>(sizeof(pkt));
    pkt.header.flags   = 0;
    std::memcpy(out, &pkt, sizeof(pkt));
}
 
