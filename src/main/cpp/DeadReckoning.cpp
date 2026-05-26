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

    // Extrapolate with gravity (9.81 m/s^2) and air/ground friction
    const float GRAVITY = -9.81f;
    const float AIR_FRICTION = 0.992f; // simple linear damping per frame-step
    const float BALL_RADIUS = 0.11f;   // standard size-5 football radius
    const float BOUNCE_ELASTICITY = 0.65f; // restitution coefficient
    const float GROUND_FRICTION = 0.95f;

    // Apply linear equations of motion
    float vy = lastServerState_.vel[1] + GRAVITY * dt;
    float py = lastServerState_.pos[1] + (lastServerState_.vel[1] + vy) * 0.5f * dt;

    float px = lastServerState_.pos[0] + lastServerState_.vel[0] * dt;
    float pz = lastServerState_.pos[2] + lastServerState_.vel[2] * dt;

    float vx = lastServerState_.vel[0] * AIR_FRICTION;
    float vz = lastServerState_.vel[2] * AIR_FRICTION;

    // Floor collision
    if (py < BALL_RADIUS) {
        py = BALL_RADIUS;
        if (vy < 0.0f) {
            vy = -vy * BOUNCE_ELASTICITY; // bounce up
        }
        // Apply ground friction
        vx *= GROUND_FRICTION;
        vz *= GROUND_FRICTION;
        if (std::abs(vy) < 0.2f) vy = 0.0f; // threshold to rest
    } else {
        vy *= AIR_FRICTION;
    }

    // Save extrapolated state back so sequential ticks accumulate correctly
    lastServerState_.pos[0] = px;
    lastServerState_.pos[1] = py;
    lastServerState_.pos[2] = pz;
    lastServerState_.vel[0] = vx;
    lastServerState_.vel[1] = vy;
    lastServerState_.vel[2] = vz;

    out = lastServerState_;
}
