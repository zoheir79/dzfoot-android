#include "GameBridge.h"
#include <cstring>

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
