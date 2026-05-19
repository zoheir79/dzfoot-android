#pragma once
#include <cstdint>

struct PlayerInput {
    float dir_x;
    float dir_z;
    float action_kick;    // 1.0 = kick
    float action_pass;    // 1.0 = pass
    float action_shot;    // 1.0 = shot
    float action_dribble; // 1.0 = dribble
    float sprint;         // 1.0 = sprint
    float reserved;       // padding / future use
};

class InputManager {
public:
    void onTouchDown(float x, float y);
    void onTouchMove(float x, float y);
    void onTouchUp();

    void setSprint(bool on) { input_.sprint = on ? 1.0f : 0.0f; }
    void setActionShot(bool on) { input_.action_shot = on ? 1.0f : 0.0f; }
    void setActionPass(bool on) { input_.action_pass = on ? 1.0f : 0.0f; }
    void setActionKick(bool on) { input_.action_kick = on ? 1.0f : 0.0f; }
    void setActionDribble(bool on) { input_.action_dribble = on ? 1.0f : 0.0f; }

    const PlayerInput& getInput() const { return input_; }
    void serialize(uint8_t* out, size_t maxLen) const;

private:
    PlayerInput input_ = {};
    float touchStartX_ = 0.0f;
    float touchStartY_ = 0.0f;
    bool touching_ = false;
};
