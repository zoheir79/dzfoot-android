#pragma once
#include <cstdint>

struct PlayerInput {
    uint8_t player_idx;
    float dir_x;
    float dir_z;
    bool sprint;
    bool action_shot;
    bool action_pass;
    bool action_tackle;
    bool action_dribble;
};

class InputManager {
public:
    void onTouchDown(float x, float y);
    void onTouchMove(float x, float y);
    void onTouchUp();

    void setSprint(bool on) { input_.sprint = on; }
    void setActionShot(bool on) { input_.action_shot = on; }
    void setActionPass(bool on) { input_.action_pass = on; }
    void setActionTackle(bool on) { input_.action_tackle = on; }
    void setActionDribble(bool on) { input_.action_dribble = on; }

    const PlayerInput& getInput() const { return input_; }
    void serialize(uint8_t* out, size_t maxLen) const;

private:
    PlayerInput input_ = {};
    float touchStartX_ = 0.0f;
    float touchStartY_ = 0.0f;
    bool touching_ = false;
};
