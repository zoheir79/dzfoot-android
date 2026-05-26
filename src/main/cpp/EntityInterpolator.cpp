#include "EntityInterpolator.h"
#include <cmath>
#include <cstring>

void EntityInterpolator::addState(const dzfoot::GameStatePacket& state) {
    head_ = (head_ + 1) % MAX_STATES;
    buffer_[head_].state = state;
    buffer_[head_].receiveTimeMs = getTimeMs(state);
    if (count_ < MAX_STATES) ++count_;
}

float EntityInterpolator::getTimeMs(const dzfoot::GameStatePacket& s) const {
    // Use server tick as a proxy for time (20 Hz = 50ms per tick)
    return s.tick * 50.0f;
}

float EntityInterpolator::latestReceivedTimeMs() const {
    if (count_ == 0) return 0.0f;
    return buffer_[head_].receiveTimeMs;
}

void EntityInterpolator::lerpVec3(const float* a, const float* b, float t, float* out) {
    out[0] = a[0] + (b[0] - a[0]) * t;
    out[1] = a[1] + (b[1] - a[1]) * t;
    out[2] = a[2] + (b[2] - a[2]) * t;
}

void EntityInterpolator::lerpFloat(float a, float b, float t, float& out) {
    out = a + (b - a) * t;
}

void EntityInterpolator::interpolate(float renderTimeMs, dzfoot::GameStatePacket& out) const {
    if (count_ == 0) return;

    // Find two states surrounding renderTimeMs
    int idxA = -1, idxB = -1;
    for (int i = 0; i < count_; ++i) {
        int idx = (head_ - i + MAX_STATES) % MAX_STATES;
        if (buffer_[idx].receiveTimeMs <= renderTimeMs) {
            idxA = idx;
            idxB = (idx + 1) % MAX_STATES;
            if (i == 0) idxB = head_; // edge case
            break;
        }
    }

    if (idxA < 0) {
        // renderTime older than oldest state: use oldest
        out = buffer_[(head_ - count_ + 1 + MAX_STATES) % MAX_STATES].state;
        return;
    }

    const StateSnapshot& snapA = buffer_[idxA];
    const StateSnapshot& snapB = buffer_[idxB];

    float range = snapB.receiveTimeMs - snapA.receiveTimeMs;
    float t = (range > 0.0001f) ? (renderTimeMs - snapA.receiveTimeMs) / range : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    out = snapA.state; // copy base

    // Interpolate ball
    lerpVec3(snapA.state.ball.pos, snapB.state.ball.pos, t, out.ball.pos);
    lerpFloat(snapA.state.ball.vel[0], snapB.state.ball.vel[0], t, out.ball.vel[0]);
    lerpFloat(snapA.state.ball.vel[1], snapB.state.ball.vel[1], t, out.ball.vel[1]);
    lerpFloat(snapA.state.ball.vel[2], snapB.state.ball.vel[2], t, out.ball.vel[2]);

    // Interpolate players
    for (int i = 0; i < dzfoot::DZ_MAX_PLAYERS; ++i) {
        lerpVec3(snapA.state.players[i].pos, snapB.state.players[i].pos, t, out.players[i].pos);
        lerpFloat(snapA.state.players[i].vel[0], snapB.state.players[i].vel[0], t, out.players[i].vel[0]);
        lerpFloat(snapA.state.players[i].vel[1], snapB.state.players[i].vel[1], t, out.players[i].vel[1]);
        lerpFloat(snapA.state.players[i].vel[2], snapB.state.players[i].vel[2], t, out.players[i].vel[2]);
    }
}
