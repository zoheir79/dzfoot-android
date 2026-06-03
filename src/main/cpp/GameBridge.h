#pragma once
#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>
#include "protocol/DZFootProtocol.h"
#include "EntityInterpolator.h"
#include "PredictionBuffer.h"
#include "DeadReckoning.h"

class GameBridge {
public:
    GameBridge();
    void applyGameState(const uint8_t* data, size_t len);
    void applyMatchEvent(const uint8_t* data, size_t len);
    void applyTacticalState(const uint8_t* data, size_t len);

    // Raw server state (for prediction/reconciliation)
    dzfoot::GameStatePacket currentState() const;
    dzfoot::TacticalStatePacket tacticalState() const;

    // Interpolated state for rendering (smooth 20 Hz)
    dzfoot::GameStatePacket getInterpolatedState();

    std::vector<dzfoot::MatchEventPacket> flushEvents();

    void setARCamera(const float* viewMatrix, const float* projMatrix, const float* anchorMatrix);

    // Anti-lag accessors
    EntityInterpolator& interpolator() { return interpolator_; }
    PredictionBuffer& predictor() { return predictor_; }
    DeadReckoning& ballDr() { return deadReckoning_; }

    // Call each frame with delta-time (seconds) for dead reckoning update
    void tick(float dt);

private:
    dzfoot::GameStatePacket state_;
    dzfoot::TacticalStatePacket tacticalState_;
    std::atomic<uint32_t> lastAppliedTick_ = 0;
    uint32_t lastTacticalTick_ = 0;
    std::vector<dzfoot::MatchEventPacket> pendingEvents_;
    float arViewMatrix_[16];
    float arProjMatrix_[16];
    float arAnchorMatrix_[16];
    bool useARCamera_ = false;

    EntityInterpolator interpolator_;
    PredictionBuffer predictor_;
    DeadReckoning deadReckoning_;

    double lastPacketLocalTimeMs_ = 0.0;
    float lastPacketServerTimeMs_ = 0.0f;
    mutable std::mutex mutex_;
};
