#include "GameBridge.h"
#include <cstring>
#include <android/log.h>

#define LOG_TAG "ARRenderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

GameBridge::GameBridge() {
    std::memset(&state_, 0, sizeof(state_));
    std::memset(&tacticalState_, 0, sizeof(tacticalState_));
    // Formation 4-3-3 Team A (left side, blue) — all players in own half (X<0)
    float teamA[11][3] = {
        {-0.85f,  0.00f, 0.0f}, // GK
        {-0.70f,  0.30f, 0.0f}, {-0.75f,  0.10f, 0.0f}, {-0.75f, -0.10f, 0.0f}, {-0.70f, -0.30f, 0.0f}, // DEF
        {-0.45f,  0.20f, 0.0f}, {-0.55f,  0.00f, 0.0f}, {-0.45f, -0.20f, 0.0f}, // MID
        {-0.15f,  0.25f, 0.0f}, {-0.20f,  0.00f, 0.0f}, {-0.15f, -0.25f, 0.0f}  // FWD (near center, still left half)
    };
    // Formation 4-3-3 Team B (right side, red) — all players in own half (X>0)
    float teamB[11][3] = {
        { 0.85f,  0.00f, 0.0f}, // GK
        { 0.70f, -0.30f, 0.0f}, { 0.75f, -0.10f, 0.0f}, { 0.75f,  0.10f, 0.0f}, { 0.70f,  0.30f, 0.0f}, // DEF
        { 0.45f, -0.20f, 0.0f}, { 0.55f,  0.00f, 0.0f}, { 0.45f,  0.20f, 0.0f}, // MID
        { 0.15f, -0.25f, 0.0f}, { 0.20f,  0.00f, 0.0f}, { 0.15f,  0.25f, 0.0f}  // FWD (near center, still right half)
    };
    for (int i = 0; i < 11; ++i) {
        state_.players[i].pos[0] = teamA[i][0];
        state_.players[i].pos[1] = teamA[i][1];
        state_.players[i].pos[2] = teamA[i][2];
        state_.players[i].team = 0;
        state_.players[i].flags = 1; // is_active
        state_.players[i].rotY = 1.57079633f; // face +X (attack right): modelYaw = rotY (+Z model)
    }
    for (int i = 0; i < 11; ++i) {
        state_.players[i+11].pos[0] = teamB[i][0];
        state_.players[i+11].pos[1] = teamB[i][1];
        state_.players[i+11].pos[2] = teamB[i][2];
        state_.players[i+11].team = 1;
        state_.players[i+11].flags = 1; // is_active
        state_.players[i+11].rotY = -1.57079633f; // face -X (attack left): modelYaw = rotY (+Z model)
    }
    state_.ball.pos[0] = 0; state_.ball.pos[1] = 0; state_.ball.pos[2] = 0.25f;
}

void GameBridge::applyGameState(const uint8_t* data, size_t len) {
    if (!dzfoot::validateGameStatePacket(data, len)) {
        LOGE("Rejected invalid GameState packet (len=%zu)", len);
        // Dump first 16 bytes for debugging
        char hex[49];
        for (int i = 0; i < 16 && i < (int)len; ++i) {
            sprintf(hex + i*3, "%02x ", data[i]);
        }
        LOGI("GameState header hex: %s", hex);
        return;
    }
    std::memcpy(&state_, data, sizeof(dzfoot::GameStatePacket));
    static int gsLogCounter = 0;
    static bool firstGsLogged = false;
    bool isFirst = !firstGsLogged;
    if (isFirst) firstGsLogged = true;
    if (isFirst || gsLogCounter++ % 60 == 0) {
        LOGI("GameState tick=%u pos0=(%.3f,%.3f,%.3f) anim0=%d rotY0=%.3f flags0=%d",
             state_.tick, state_.players[0].pos[0], state_.players[0].pos[1],
             state_.players[0].pos[2], (int)state_.players[0].anim, state_.players[0].rotY,
             (int)state_.players[0].flags);
        // Verbose: log all 22 players for first packet, then first 6 per team
        int logCount = isFirst ? 22 : 6;
        for (int t = 0; t < 2; ++t) {
            int base = t * 11;
            LOGI("[ServerRAW] Team%d positions (first=%d):", t, (int)isFirst);
            for (int i = 0; i < logCount && i < 11; ++i) {
                const auto& p = state_.players[base + i];
                LOGI("  P%d pos=(%.3f,%.3f,%.3f) team=%d rotY=%.3f flags=%d",
                     base+i, p.pos[0], p.pos[1], p.pos[2], (int)p.team, p.rotY, (int)p.flags);
            }
        }
    }
    // Sanity-check first player position to catch NaN
    if (!dzfoot::isFiniteVec3(state_.players[0].pos)) {
        LOGE("Rejected GameState with NaN in player position");
        return;
    }

    if (state_.tick < lastAppliedTick_) {
        // If the new tick is smaller than the last applied tick,
        // it means the server has reset (new match started or server restarted).
        // Reset our state and clear the interpolator.
        LOGI("Server tick reset detected (new=%u, old=%u). Clearing interpolator.", state_.tick, lastAppliedTick_);
        interpolator_.clear();
        lastAppliedTick_ = 0;
    }

    if (state_.tick <= lastAppliedTick_) {
        LOGI("GameState tick=%u <= lastAppliedTick_=%u — skipping (duplicate/out-of-order)",
             state_.tick, lastAppliedTick_);
        return;
    }
    lastAppliedTick_ = state_.tick;

    // Feed anti-lag systems
    interpolator_.addState(state_);
    float serverTimeMs = state_.tick * (1000.0f / 60.0f);
    deadReckoning_.update(state_.ball, serverTimeMs);

    // Server reconciliation stub: acknowledge up to this tick
    float delta[3];
    predictor_.acknowledge(state_.tick, state_.players[0], delta);
}

dzfoot::GameStatePacket GameBridge::getInterpolatedState() {
    dzfoot::GameStatePacket out = state_;
    float latest = interpolator_.latestReceivedTimeMs();
    float renderTime = latest - EntityInterpolator::INTERP_DELAY_MS;
    if (renderTime < 0.0f) renderTime = 0.0f;
    interpolator_.interpolate(renderTime, out);

    return out;
}

void GameBridge::tick(float dt) {
    // Dead reckoning runs continuously each frame
    // Other systems are event-driven or per-packet
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

void GameBridge::applyTacticalState(const uint8_t* data, size_t len) {
    if (!dzfoot::validateTacticalStatePacket(data, len)) {
        LOGE("Rejected invalid TacticalState packet (len=%zu)", len);
        return;
    }
    dzfoot::TacticalStatePacket incoming;
    std::memcpy(&incoming, data, sizeof(dzfoot::TacticalStatePacket));
    if (incoming.tick <= lastTacticalTick_) {
        return;
    }
    tacticalState_ = incoming;
    lastTacticalTick_ = incoming.tick;
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
    float speed = p.vel[0] * p.vel[0] + p.vel[1] * p.vel[1];
    if (speed < 0.01f) return dzfoot::ANIM_IDLE;
    if (speed < 0.16f) return dzfoot::ANIM_WALK;
    if (speed < 0.64f) return dzfoot::ANIM_RUN;
    return dzfoot::ANIM_SPRINT;
}
 
