#include "GameBridge.h"
#include <cstring>
#include <android/log.h>

#define LOG_TAG "GameBridge"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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
    if (!dzfoot::validateGameStatePacket(data, len)) {
        LOGE("Rejected invalid GameState packet (len=%zu)", len);
        return;
    }
    std::memcpy(&state_, data, sizeof(dzfoot::GameStatePacket));
    // Sanity-check first player position to catch NaN
    if (!dzfoot::isFiniteVec3(state_.players[0].pos)) {
        LOGE("Rejected GameState with NaN in player position");
        return;
    }
}

void GameBridge::applyMatchEvent(const uint8_t* data, size_t len) {
    if (!dzfoot::validateMatchEventPacket(data, len)) {
        LOGE("Rejected invalid MatchEvent packet (len=%zu)", len);
        return;
    }
    dzfoot::MatchEventPacket ev;
    std::memcpy(&ev, data, sizeof(dzfoot::MatchEventPacket));
    pendingEvents_.push_back(ev);
}

std::vector<dzfoot::MatchEventPacket> GameBridge::flushEvents() {
    std::vector<dzfoot::MatchEventPacket> result = std::move(pendingEvents_);
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

// AnimationStateDetector -- deduce anim_id from GF state
// In real integration this runs inside GameplayFootball server tick
uint8_t deduceAnimId(const dzfoot::NetworkPlayerState& p, const dzfoot::NetworkBallState& ball) {
    float speed = p.vel[0] * p.vel[0] + p.vel[2] * p.vel[2];
    if (speed < 0.01f) return dzfoot::ANIM_IDLE;
    if (speed < 0.16f) return dzfoot::ANIM_WALK;
    if (speed < 0.64f) return dzfoot::ANIM_RUN;
    return dzfoot::ANIM_SPRINT;
}
 
