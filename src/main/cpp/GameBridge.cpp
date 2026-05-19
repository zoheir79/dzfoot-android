#include "GameBridge.h"
#include <cstring>

GameBridge::GameBridge() {
    std::memset(&state_, 0, sizeof(state_));
    // Formation 4-4-2 Team A (left side, blue)
    float teamA[11][3] = {
        {-5.5f, 0, 0},       // GK
        {-4.0f, 0, -2.0f}, {-4.0f, 0, -0.7f}, {-4.0f, 0, 0.7f}, {-4.0f, 0, 2.0f},  // DEF
        {-2.0f, 0, -2.0f}, {-2.0f, 0, -0.7f}, {-2.0f, 0, 0.7f}, {-2.0f, 0, 2.0f},  // MID
        {-0.5f, 0, -1.0f}, {-0.5f, 0, 1.0f}                                            // FWD
    };
    // Formation 4-4-2 Team B (right side, red)
    float teamB[11][3] = {
        {5.5f, 0, 0},       // GK
        {4.0f, 0, -2.0f}, {4.0f, 0, -0.7f}, {4.0f, 0, 0.7f}, {4.0f, 0, 2.0f},  // DEF
        {2.0f, 0, -2.0f}, {2.0f, 0, -0.7f}, {2.0f, 0, 0.7f}, {2.0f, 0, 2.0f},  // MID
        {0.5f, 0, -1.0f}, {0.5f, 0, 1.0f}                                            // FWD
    };
    for (int i = 0; i < 11; ++i) {
        state_.players[i].pos[0] = teamA[i][0];
        state_.players[i].pos[1] = teamA[i][1];
        state_.players[i].pos[2] = teamA[i][2];
        state_.players[i].team = 0;
    }
    for (int i = 0; i < 11; ++i) {
        state_.players[i+11].pos[0] = teamB[i][0];
        state_.players[i+11].pos[1] = teamB[i][1];
        state_.players[i+11].pos[2] = teamB[i][2];
        state_.players[i+11].team = 1;
    }
    state_.ball.pos[0] = 0; state_.ball.pos[1] = 0.25f; state_.ball.pos[2] = 0;
}

void GameBridge::applyGameState(const uint8_t* data, size_t len) {
    if (len < sizeof(GameState)) return;
    std::memcpy(&state_, data, sizeof(GameState));
}

void GameBridge::applyMatchEvent(const uint8_t* data, size_t len) {
    if (len < sizeof(MatchEvent)) return;
    MatchEvent ev;
    std::memcpy(&ev, data, sizeof(MatchEvent));
    pendingEvents_.push_back(ev);
}

std::vector<MatchEvent> GameBridge::flushEvents() {
    std::vector<MatchEvent> result = std::move(pendingEvents_);
    pendingEvents_.clear();
    return result;
}

void GameBridge::setARCamera(const float* viewMatrix,
                             const float* projMatrix,
                             const float* anchorMatrix) {
    std::memcpy(arViewMatrix_, viewMatrix, 16 * sizeof(float));
    std::memcpy(arProjMatrix_, projMatrix, 16 * sizeof(float));
    std::memcpy(arAnchorMatrix_, anchorMatrix, 16 * sizeof(float));
    useARCamera_ = true;
}

// AnimationStateDetector — deduce anim_id from GF state
// In real integration this runs inside GameplayFootball server tick
uint8_t deduceAnimId(const PlayerState& p, const BallState& ball) {
    float speed = p.vel[0] * p.vel[0] + p.vel[2] * p.vel[2];
    if (speed < 0.01f) return 0;  // IDLE
    if (speed < 0.16f) return 1;  // WALK
    if (speed < 0.64f) return 2;  // RUN
    return 3;  // SPRINT
}
 
