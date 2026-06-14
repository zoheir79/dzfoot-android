#include "TouchController.h"
#include <algorithm>
#include <chrono>

static double nowMs() {
    auto t = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t.time_since_epoch()).count();
}

void TouchController::setScreenSize(int width, int height) {
    screenW_ = width;
    screenH_ = height;
    // Joystick zone: bottom-left, 200px from edges, 140px radius
    joyCx_ = 200.0f;
    joyCy_ = height - 200.0f;
    joyRadius_ = 140.0f;

    // Action buttons: bottom-right, arranged in a clean, ergonomic diamond
    float btnR = 64.0f;
    float rightX = width - 240.0f;
    float baseY = height - 200.0f;

    buttons_[0] = { "PASS",   rightX,          baseY,           btnR,        dzfoot::BUTTON_PASS,          0xFF44AA44, false, -1, 0.0 };
    buttons_[1] = { "SHOT",   rightX - 140.0f, baseY - 140.0f,  btnR,        dzfoot::BUTTON_SHOT,          0xFFFF4444, false, -1, 0.0 };
    buttons_[2] = { "LOB",    rightX,          baseY - 280.0f,  btnR,        dzfoot::BUTTON_HIGH_PASS,     0xFFAA44FF, false, -1, 0.0 };
    buttons_[3] = { "TACKLE", rightX + 140.0f, baseY - 140.0f,  btnR,        dzfoot::BUTTON_SLIDING,       0xFF4488FF, false, -1, 0.0 };
    buttons_[4] = { "SPRINT", rightX + 140.0f, baseY - 320.0f,  btnR * 0.9f, dzfoot::BUTTON_SPRINT,         0xFFFFAA00, false, -1, 0.0 };
    buttons_[5] = { "SWITCH", rightX - 140.0f, baseY - 320.0f,  btnR * 0.9f, dzfoot::BUTTON_SWITCH_PLAYER,  0xFFAAAAAA, false, -1, 0.0 };
    numButtons_ = 6;
    buttonsInitialized_ = true;
}

void TouchController::onTouchDown(int pointerId, float x, float y) {
    if (pointerId < 0 || pointerId >= MAX_POINTERS) return;
    pointers_[pointerId] = { true, x, y };

    // Check joystick zone first (left half, bottom area)
    float djoy = std::sqrt((x - joyCx_)*(x - joyCx_) + (y - joyCy_)*(y - joyCy_));
    if (djoy < joyRadius_ * 1.5f && joyPointer_ < 0) {
        joyPointer_ = pointerId;
        updateJoystick(pointerId, x, y);
    }

    // Check action buttons
    for (int i = 0; i < numButtons_; ++i) {
        float d = std::sqrt((x - buttons_[i].cx)*(x - buttons_[i].cx) + (y - buttons_[i].cy)*(y - buttons_[i].cy));
        if (d < buttons_[i].radius && buttons_[i].pointerId < 0) {
            buttons_[i].pressed = true;
            buttons_[i].pointerId = pointerId;
            buttons_[i].pressTimeMs = nowMs();
            input_.buttons |= buttons_[i].buttonMask;
        }
    }
    updateButtonStates();
}

void TouchController::onTouchMove(int pointerId, float x, float y) {
    if (pointerId < 0 || pointerId >= MAX_POINTERS) return;
    if (!pointers_[pointerId].active) return;
    pointers_[pointerId].x = x;
    pointers_[pointerId].y = y;

    if (joyPointer_ == pointerId) {
        updateJoystick(pointerId, x, y);
    }
}

void TouchController::onTouchUp(int pointerId) {
    if (pointerId < 0 || pointerId >= MAX_POINTERS) return;
    pointers_[pointerId].active = false;

    if (joyPointer_ == pointerId) {
        joyPointer_ = -1;
        joyActive_ = false;
        joyStickX_ = 0;
        joyStickY_ = 0;
        input_.dirX = 0;
        input_.dirZ = 0;
    }

    for (int i = 0; i < numButtons_; ++i) {
        if (buttons_[i].pointerId == pointerId) {
            buttons_[i].pressed = false;
            buttons_[i].pointerId = -1;
            input_.buttons &= ~buttons_[i].buttonMask;
        }
    }
    updateButtonStates();
}

void TouchController::updateJoystick(int pointerId, float x, float y) {
    float dx = x - joyCx_;
    float dy = y - joyCy_;
    float len = std::sqrt(dx*dx + dy*dy);
    float maxR = joyRadius_;
    if (len > maxR) {
        dx = (dx / len) * maxR;
        dy = (dy / len) * maxR;
        len = maxR;
    }
    joyStickX_ = dx / maxR;
    joyStickY_ = dy / maxR;
    joyActive_ = len > 5.0f;

    // Map to game direction: screen X is world X, screen Y down is world Z forward
    input_.dirX = joyStickX_;
    input_.dirZ = -joyStickY_; // invert Y so up on stick = forward
}

void TouchController::updateButtonStates() {
    // recompute aim indicator based on active buttons + joystick direction
    bool anyAction = (input_.buttons & (dzfoot::BUTTON_PASS | dzfoot::BUTTON_SHOT | dzfoot::BUTTON_KICK | dzfoot::BUTTON_HIGH_PASS)) != 0;
    if (anyAction && (std::fabs(input_.dirX) > 0.1f || std::fabs(input_.dirZ) > 0.1f)) {
        aim_.visible = true;
        aim_.dirX = input_.dirX;
        aim_.dirY = input_.dirZ;
        aim_.power = std::min(1.0f, std::sqrt(input_.dirX*input_.dirX + input_.dirZ*input_.dirZ));
        if (input_.buttons & dzfoot::BUTTON_SHOT) aim_.color = 0xFFFF4444;
        else if (input_.buttons & dzfoot::BUTTON_PASS) aim_.color = 0xFF44FF44;
        else if (input_.buttons & dzfoot::BUTTON_HIGH_PASS) aim_.color = 0xFFAA44FF;
        else aim_.color = 0xFFFFFFFF;
    } else {
        aim_.visible = false;
    }
}

void TouchController::setActionShot(bool on)      { if (on) input_.buttons |= dzfoot::BUTTON_SHOT; else input_.buttons &= ~dzfoot::BUTTON_SHOT; updateButtonStates(); }
void TouchController::setActionPass(bool on)       { if (on) input_.buttons |= dzfoot::BUTTON_PASS; else input_.buttons &= ~dzfoot::BUTTON_PASS; updateButtonStates(); }
void TouchController::setActionKick(bool on)       { if (on) input_.buttons |= dzfoot::BUTTON_KICK; else input_.buttons &= ~dzfoot::BUTTON_KICK; updateButtonStates(); }
void TouchController::setActionDribble(bool on)     { if (on) input_.buttons |= dzfoot::BUTTON_DRIBBLE; else input_.buttons &= ~dzfoot::BUTTON_DRIBBLE; updateButtonStates(); }
void TouchController::setActionHighPass(bool on)    { if (on) input_.buttons |= dzfoot::BUTTON_HIGH_PASS; else input_.buttons &= ~dzfoot::BUTTON_HIGH_PASS; updateButtonStates(); }
void TouchController::setActionSliding(bool on)     { if (on) input_.buttons |= dzfoot::BUTTON_SLIDING; else input_.buttons &= ~dzfoot::BUTTON_SLIDING; updateButtonStates(); }
void TouchController::setActionSwitchPlayer(bool on){ if (on) input_.buttons |= dzfoot::BUTTON_SWITCH_PLAYER; else input_.buttons &= ~dzfoot::BUTTON_SWITCH_PLAYER; updateButtonStates(); }
void TouchController::setSprint(bool on)            { if (on) input_.buttons |= dzfoot::BUTTON_SPRINT; else input_.buttons &= ~dzfoot::BUTTON_SPRINT; updateButtonStates(); }

void TouchController::setActivePlayer(uint8_t team, uint8_t playerIdx) {
    activeTeam_ = team;
    activePlayerIdx_ = playerIdx;
}

void TouchController::applyCameraRotation(float camFwdX, float camFwdZ) {
    // Normalize camera forward on XZ plane
    float len = std::sqrt(camFwdX * camFwdX + camFwdZ * camFwdZ);
    if (len < 0.0001f) return;
    float fx = camFwdX / len;
    float fz = camFwdZ / len;
    // Camera right = perpendicular to forward on XZ (right-hand rule, Y-up)
    float rx =  fz;
    float rz = -fx;

    // input_.dirX = joystick right (+screen X)
    // input_.dirZ = joystick up    (+screen -Y, mapped to world +Z in updateJoystick)
    float inX = input_.dirX;
    float inZ = input_.dirZ;

    input_.dirX = inX * rx + inZ * fx;
    input_.dirZ = inX * rz + inZ * fz;

    // Also rotate aim indicator so pass/shot direction follows camera
    if (aim_.visible) {
        float aX = aim_.dirX;
        float aY = aim_.dirY;
        aim_.dirX = aX * rx + aY * fx;
        aim_.dirY = aX * rz + aY * fz;
    }
}

void TouchController::serialize(uint8_t* out, size_t maxLen) const {
    if (maxLen < sizeof(dzfoot::PlayerInputPacket)) return;
    dzfoot::PlayerInputPacket pkt = input_;
    pkt.team       = activeTeam_;
    pkt.playerIdx  = activePlayerIdx_;
    pkt.header.magic   = dzfoot::DZ_MAGIC;
    pkt.header.version = dzfoot::DZ_PROTOCOL_VERSION;
    pkt.header.type    = dzfoot::PACKET_PLAYER_INPUT;
    pkt.header.size    = static_cast<uint16_t>(sizeof(pkt));
    pkt.header.flags   = 0;
    std::memcpy(out, &pkt, sizeof(pkt));
}

TouchController::JoystickState TouchController::getJoystickState() const {
    return { joyCx_, joyCy_, joyStickX_, joyStickY_, joyRadius_, joyActive_ };
}

int TouchController::getActionButtons(ButtonState out[MAX_ACTION_BUTTONS]) const {
    double t = nowMs();
    for (int i = 0; i < numButtons_; ++i) {
        float power = 0.0f;
        if (buttons_[i].pressed) {
            double elapsed = t - buttons_[i].pressTimeMs;
            // GF gauge: baseTime_ms=60, max=1000ms → clamp to [0,1]
            power = static_cast<float>(std::clamp((elapsed - 60.0) / 940.0, 0.0, 1.0));
        }
        out[i] = { buttons_[i].label, buttons_[i].cx, buttons_[i].cy, buttons_[i].radius, buttons_[i].pressed, buttons_[i].color, power };
    }
    return numButtons_;
}

float TouchController::getChargePower(uint16_t buttonMask) const {
    double t = nowMs();
    for (int i = 0; i < numButtons_; ++i) {
        if (buttons_[i].buttonMask == buttonMask && buttons_[i].pressed) {
            double elapsed = t - buttons_[i].pressTimeMs;
            return static_cast<float>(std::clamp((elapsed - 60.0) / 940.0, 0.0, 1.0));
        }
    }
    return 0.0f;
}

void TouchController::updateRadar(const dzfoot::GameStatePacket& gs) {
    numRadarDots_ = 0;
    // Ball
    if (numRadarDots_ < MAX_RADAR_DOTS) {
        radarDots_[numRadarDots_++] = { gs.ball.pos[0], gs.ball.pos[1], 0xFFFFFFFF, true };
    }
    // Players
    for (int i = 0; i < 22 && numRadarDots_ < MAX_RADAR_DOTS; ++i) {
        uint32_t col = (gs.players[i].team == 0) ? 0xFF4488FF : 0xFFFF8844;
        // Controlled player highlighted
        if (gs.players[i].flags & 4) col = 0xFFFFFF00;
        radarDots_[numRadarDots_++] = { gs.players[i].pos[0], gs.players[i].pos[1], col, false };
    }
}

int TouchController::getRadarDots(RadarDot out[MAX_RADAR_DOTS]) const {
    int n = std::min(numRadarDots_, MAX_RADAR_DOTS);
    std::memcpy(out, radarDots_, n * sizeof(RadarDot));
    return n;
}
