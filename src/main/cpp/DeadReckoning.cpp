#include "DeadReckoning.h"
#include <cmath>

void DeadReckoning::reset() {
    hasState_ = false;
    localTimeMs_ = 0.0f;
}

void DeadReckoning::update(const dzfoot::NetworkBallState& serverState, float serverTimeMs) {
    lastServerState_ = serverState;
    lastServerTimeMs_ = serverTimeMs;
    localTimeMs_ = serverTimeMs;
    hasState_ = true;
}

void DeadReckoning::tick(float deltaTimeMs, dzfoot::NetworkBallState& out) {
    if (!hasState_) {
        out = {};
        return;
    }

    localTimeMs_ += deltaTimeMs;
    float dt = deltaTimeMs * 0.001f; // to seconds

    // Extrapolate using last known velocity
    out.pos[0] = lastServerState_.pos[0] + lastServerState_.vel[0] * dt;
    out.pos[1] = lastServerState_.pos[1] + lastServerState_.vel[1] * dt;
    out.pos[2] = lastServerState_.pos[2] + lastServerState_.vel[2] * dt;
    out.vel[0] = lastServerState_.vel[0];
    out.vel[1] = lastServerState_.vel[1];
    out.vel[2] = lastServerState_.vel[2];

    // Smooth correction toward server position (if we had continuous server updates)
    // For now, basic dead reckoning without smoothing — server update resets state
}
