#pragma once
#include "protocol/DZFootProtocol.h"

// Extrapolates ball position between GameState updates using velocity
// Smoothly corrects when server diverges
class DeadReckoning {
public:
    void update(const dzfoot::NetworkBallState& serverState, float serverTimeMs);

    // Call each frame with deltaTimeMs (time since last call)
    void tick(float deltaTimeMs, dzfoot::NetworkBallState& out);

    void reset();

private:
    dzfoot::NetworkBallState lastServerState_;
    float lastServerTimeMs_ = 0.0f;
    float localTimeMs_ = 0.0f;
    bool hasState_ = false;

    // Smooth correction: factor per second toward server position
    static constexpr float CORRECTION_SPEED = 5.0f;
};
