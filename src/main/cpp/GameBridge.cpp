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
    if (len < sizeof(dzfoot::GameStatePacket)) {
        LOGE("GameBridge: packet too small: %zu (expected %zu)", len, sizeof(dzfoot::GameStatePacket));
        return;
    }
    std::memcpy(&state_, data, sizeof(dzfoot::GameStatePacket));

    // Sanity-check first player position to catch NaN
    if (!dzfoot::isFiniteVec3(state_.players[0].pos)) {
        LOGE("Rejected GameState with NaN in player position");
        return;
    }

    static int recvCounter = 0;
    if ((recvCounter++ % 60) == 0) {
        LOGI("[gamestates] GameBridge recv tick=%u size=%zu ball=(%.3f,%.3f,%.3f) cam=(%.3f,%.3f,%.3f) fov=%.1f p0=(%.3f,%.3f) p6=(%.3f,%.3f)",
             state_.tick, len,
             state_.ball.pos[0], state_.ball.pos[1], state_.ball.pos[2],
             state_.camera.pos[0], state_.camera.pos[1], state_.camera.pos[2],
             state_.camera.fov,
             state_.players[0].pos[0], state_.players[0].pos[1],
             state_.players[6].pos[0], state_.players[6].pos[1]);
    }

    if (state_.tick < lastAppliedTick_ && lastAppliedTick_ - state_.tick > 10) {
        // Server reset detected (new match or restart)
        LOGI("[gamestates] Server tick reset detected (new=%u, old=%u). Clearing interpolator.", state_.tick, lastAppliedTick_.load());
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
    if ((interpCounter++ % 120) == 0) {
        LOGI("[gamestates] interp tick=%u ball=(%.3f,%.3f,%.3f) p0=(%.3f,%.3f) p6=(%.3f,%.3f)",
             out.tick, out.ball.pos[0], out.ball.pos[1], out.ball.pos[2],
             out.players[0].pos[0], out.players[0].pos[1],
             out.players[6].pos[0], out.players[6].pos[1]);
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
 
