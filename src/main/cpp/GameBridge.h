#pragma once
#include <cstdint>
#include <vector>
#include "protocol/DZFootProtocol.h"
#include "EntityInterpolator.h"
#include "PredictionBuffer.h"
#include "DeadReckoning.h"

class GameBridge {
public:
    GameBridge();
    void applyGameState(const uint8_t* data, size_t len);
    void applyMatchEvent(const uint8_t* data, size_t len);

    // Raw server state (for prediction/reconciliation)
    const dzfoot::GameStatePacket& currentState() const { return state_; }

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
    std::vector<dzfoot::MatchEventPacket> pendingEvents_;
    float arViewMatrix_[16];
    float arProjMatrix_[16];
    float arAnchorMatrix_[16];
    bool useARCamera_ = false;

    EntityInterpolator interpolator_;
    PredictionBuffer predictor_;
    DeadReckoning deadReckoning_;
};
