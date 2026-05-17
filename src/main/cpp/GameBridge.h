#pragma once
#include <cstdint>
#include <vector>

struct PlayerState {
    float pos[3];
    float vel[3];
    float rot;
    uint8_t anim_id;
    uint8_t team;
};

struct BallState {
    float pos[3];
    float vel[3];
};

struct GameState {
    PlayerState players[22];
    BallState ball;
    int score[2];
    float timer;
    uint8_t game_mode;
    uint32_t tick;
};

struct MatchEvent {
    uint8_t event_type;
    uint8_t team;
    uint8_t player_idx;
    float pos[3];
    uint32_t tick;
    int score[2];
};

class GameBridge {
public:
    GameBridge();
    void applyGameState(const uint8_t* data, size_t len);
    void applyMatchEvent(const uint8_t* data, size_t len);

    const GameState& currentState() const { return state_; }
    std::vector<MatchEvent> flushEvents();

    void setARCamera(const float* viewMatrix, const float* projMatrix, const float* anchorMatrix);

private:
    GameState state_;
    std::vector<MatchEvent> pendingEvents_;
    float arViewMatrix_[16];
    float arProjMatrix_[16];
    float arAnchorMatrix_[16];
    bool useARCamera_ = false;
};
