#include "GameBridge.h"
#include <cstring>
#include <chrono>
#include <cstdio>
#include <android/log.h>

#define LOG_TAG "ARRenderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static double getSteadyTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now.time_since_epoch()).count();
}

GameBridge::GameBridge() {
    std::memset(&state_, 0, sizeof(state_));
    std::memset(&tacticalState_, 0, sizeof(tacticalState_));
    // All players start inactive (flags=0) until the first server packet arrives.
    // This avoids the "teleportation" snap caused by a hardcoded placeholder
    // formation that did not match GF's actual kickoff positions.
    for (int i = 0; i < dzfoot::DZ_MAX_PLAYERS; ++i) {
        state_.players[i].flags = 0; // inactive until first server state
    }
    state_.ball.pos[0] = 0; state_.ball.pos[1] = 0; state_.ball.pos[2] = 0.25f;
}

void GameBridge::applyGameState(const uint8_t* data, size_t len) {
    if (!dzfoot::validateGameStatePacket(data, len)) {
        LOGE("Rejected invalid GameState packet (len=%zu)", len);
        return;
    }

    // Fast-path: read tick without copying the whole packet or taking the mutex.
    // This eliminates mutex contention for the common case of duplicate/out-of-order packets.
    uint32_t incomingTick = *reinterpret_cast<const uint32_t*>(data + offsetof(dzfoot::GameStatePacket, tick));
    if (incomingTick <= lastAppliedTick_ && incomingTick >= lastAppliedTick_ - 10) {
        // Silently drop duplicates and slightly out-of-order packets without logging.
        // The -10 margin handles jitter; anything older is treated as a reset.
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::memcpy(&state_, data, sizeof(dzfoot::GameStatePacket));

    // Sanity-check first player position to catch NaN
    if (!dzfoot::isFiniteVec3(state_.players[0].pos)) {
        LOGE("Rejected GameState with NaN in player position");
        return;
    }

    // Diagnostic: dump raw bytes of player 0 and player 11 X positions to verify sign
    static int recvCounter = 0;
    if ((recvCounter++ % 20) == 0) {
        const uint8_t* p0 = data + offsetof(dzfoot::GameStatePacket, players) + 0 * sizeof(dzfoot::NetworkPlayerState);
        const uint8_t* p11 = data + offsetof(dzfoot::GameStatePacket, players) + 11 * sizeof(dzfoot::NetworkPlayerState);
        LOGI("[GameBridge] pkt tick=%u  p0.pos[0] bytes=%02X%02X%02X%02X float=%.6f  p11.pos[0] bytes=%02X%02X%02X%02X float=%.6f",
             state_.tick,
             p0[0], p0[1], p0[2], p0[3], state_.players[0].pos[0],
             p11[0], p11[1], p11[2], p11[3], state_.players[11].pos[0]);
    }

    if (state_.tick < lastAppliedTick_ && lastAppliedTick_ - state_.tick > 10) {
        // Server reset detected (new match or restart)
        LOGI("Server tick reset detected (new=%u, old=%u). Clearing interpolator.", state_.tick, lastAppliedTick_.load());
        interpolator_.clear();
        lastAppliedTick_ = 0;
        lastPacketLocalTimeMs_ = 0.0;
        lastPacketServerTimeMs_ = 0.0f;
    }

    if (state_.tick <= lastAppliedTick_) {
        return; // duplicate after reset check
    }
    lastAppliedTick_ = state_.tick;

    // Feed anti-lag systems
    interpolator_.addState(state_);
    float serverTimeMs = state_.tick * (1000.0f / static_cast<float>(dzfoot::kSimFrequencyHz));
    deadReckoning_.update(state_.ball, serverTimeMs);

    lastPacketServerTimeMs_ = serverTimeMs;
    lastPacketLocalTimeMs_ = getSteadyTimeMs();

    // Server reconciliation stub: acknowledge up to this tick
    float delta[3];
    predictor_.acknowledge(state_.tick, state_.players[0], delta);
}

dzfoot::GameStatePacket GameBridge::getInterpolatedState() {
    std::lock_guard<std::mutex> lock(mutex_);
    dzfoot::GameStatePacket out = state_;
    float latest = interpolator_.latestReceivedTimeMs();
    float renderTime = latest - EntityInterpolator::INTERP_DELAY_MS;

    if (lastPacketLocalTimeMs_ > 0.0) {
        double elapsedLocal = getSteadyTimeMs() - lastPacketLocalTimeMs_;
        if (elapsedLocal > 200.0) elapsedLocal = 200.0; // limit drift in case of suspended threads
        
        float estServerTime = lastPacketServerTimeMs_ + static_cast<float>(elapsedLocal);
        renderTime = estServerTime - EntityInterpolator::INTERP_DELAY_MS;
    }

    if (renderTime < 0.0f) renderTime = 0.0f;
    interpolator_.interpolate(renderTime, out);

    static int interpCounter = 0;
    if ((interpCounter++ % 60) == 0) {
        // Log 1: GF original positions for all 22 players + ball + anim
        LOGI("[GF_POS] tick=%u ball=(%.3f,%.3f,%.3f)", out.tick, out.ball.pos[0], out.ball.pos[1], out.ball.pos[2]);
        for (int t = 0; t < 2; ++t) {
            char line[512];
            int offset = 0;
            offset += snprintf(line + offset, sizeof(line) - offset, "[GF_POS] T%d ", t);
            for (int i = 0; i < 11; ++i) {
                int idx = t * 11 + i;
                const auto& p = out.players[idx];
                if (offset < (int)sizeof(line) - 40) {
                    offset += snprintf(line + offset, sizeof(line) - offset, "P%d:(%.3f,%.3f,%u) ", idx, p.pos[0], p.pos[1], p.anim);
                }
            }
            LOGI("%s", line);
        }

        // Log 2: GK diagnostic (keep existing)
        LOGI("[GameBridge] interp tick=%u p0=%.6f p11=%.6f",
             out.tick, out.players[0].pos[0], out.players[11].pos[0]);
    }

    return out;
}

void GameBridge::tick(float dt) {
    // Dead reckoning runs continuously each frame
    // Other systems are event-driven or per-packet
}

dzfoot::GameStatePacket GameBridge::currentState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

dzfoot::TacticalStatePacket GameBridge::tacticalState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tacticalState_;
}

void GameBridge::applyMatchEvent(const uint8_t* data, size_t len) {
    if (!dzfoot::validateMatchEventPacket(data, len)) {
        LOGE("Rejected invalid MatchEvent packet (len=%zu)", len);
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    dzfoot::MatchEventPacket ev;
    std::memcpy(&ev, data, sizeof(dzfoot::MatchEventPacket));
    pendingEvents_.push_back(ev);
}

void GameBridge::applyTacticalState(const uint8_t* data, size_t len) {
    if (!dzfoot::validateTacticalStatePacket(data, len)) {
        LOGE("Rejected invalid TacticalState packet (len=%zu)", len);
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    dzfoot::TacticalStatePacket incoming;
    std::memcpy(&incoming, data, sizeof(dzfoot::TacticalStatePacket));
    if (incoming.tick <= lastTacticalTick_) {
        return;
    }
    tacticalState_ = incoming;
    lastTacticalTick_ = incoming.tick;
}

void GameBridge::applyMatchSetup(const uint8_t* data, size_t len) {
    if (!dzfoot::validateMatchSetupPacket(data, len)) {
        LOGE("Rejected invalid MatchSetup packet (len=%zu)", len);
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::memcpy(&setupPacket_, data, sizeof(dzfoot::MatchSetupPacket));
    hasSetup_ = true;
    LOGI("[GameBridge] MatchSetup received! Teams: %s vs %s", setupPacket_.teamAName, setupPacket_.teamBName);
}

dzfoot::MatchSetupPacket GameBridge::matchSetup() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return setupPacket_;
}

std::vector<dzfoot::MatchEventPacket> GameBridge::flushEvents() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<dzfoot::MatchEventPacket> result = std::move(pendingEvents_);
    pendingEvents_.clear();
    return result;
}

void GameBridge::setARCamera(const float* viewMatrix,
                             const float* projMatrix,
                             const float* anchorMatrix) {
    std::lock_guard<std::mutex> lock(mutex_);
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
 
