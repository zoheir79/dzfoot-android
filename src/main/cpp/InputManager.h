#pragma once
#include <cstdint>
#include "protocol/DZFootProtocol.h"

class InputManager {
public:
    void onTouchDown(float x, float y);
    void onTouchMove(float x, float y);
    void onTouchUp();

    void setSprint(bool on) {
        if (on) input_.buttons |= dzfoot::BUTTON_SPRINT;
        else    input_.buttons &= ~dzfoot::BUTTON_SPRINT;
    }
    void setActionShot(bool on) {
        if (on) input_.buttons |= dzfoot::BUTTON_SHOT;
        else    input_.buttons &= ~dzfoot::BUTTON_SHOT;
    }
    void setActionPass(bool on) {
        if (on) input_.buttons |= dzfoot::BUTTON_PASS;
        else    input_.buttons &= ~dzfoot::BUTTON_PASS;
    }
    void setActionKick(bool on) {
        if (on) input_.buttons |= dzfoot::BUTTON_KICK;
        else    input_.buttons &= ~dzfoot::BUTTON_KICK;
    }
    void setActionDribble(bool on) {
        if (on) input_.buttons |= dzfoot::BUTTON_DRIBBLE;
        else    input_.buttons &= ~dzfoot::BUTTON_DRIBBLE;
    }
    void setActionHighPass(bool on) {
        if (on) input_.buttons |= dzfoot::BUTTON_HIGH_PASS;
        else    input_.buttons &= ~dzfoot::BUTTON_HIGH_PASS;
    }
    void setActionSliding(bool on) {
        if (on) input_.buttons |= dzfoot::BUTTON_SLIDING;
        else    input_.buttons &= ~dzfoot::BUTTON_SLIDING;
    }
    void setActionSwitchPlayer(bool on) {
        if (on) input_.buttons |= dzfoot::BUTTON_SWITCH_PLAYER;
        else    input_.buttons &= ~dzfoot::BUTTON_SWITCH_PLAYER;
    }

    const dzfoot::PlayerInputPacket& getInput() const { return input_; }
    void serialize(uint8_t* out, size_t maxLen) const;

private:
    dzfoot::PlayerInputPacket input_ = {};
    float touchStartX_ = 0.0f;
    float touchStartY_ = 0.0f;
    bool touching_ = false;
};
