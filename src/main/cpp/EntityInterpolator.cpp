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
    return s.tick * (1000.0f / 60.0f);
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

void EntityInterpolator::lerpAngle(float a, float b, float t, float& out) {
    float diff = b - a;
    while (diff > 3.14159265f) diff -= 6.28318531f;
    while (diff < -3.14159265f) diff += 6.28318531f;
    out = a + diff * t;
}

void EntityInterpolator::interpolate(float renderTimeMs, dzfoot::GameStatePacket& out) const {
    if (count_ == 0) return;

    if (count_ < 2) {
        out = buffer_[head_].state;
        return;
    }

    // Find two states tightly bounding renderTimeMs chronologically
    int idxA = -1, idxB = -1;
    for (int i = 0; i < count_ - 1; ++i) {
        // Chronological indexes (oldest to newest)
        // Oldest is head_ - count_ + 1 (wrapped)
        int idx1 = (head_ - count_ + 1 + i + MAX_STATES) % MAX_STATES;
        int idx2 = (idx1 + 1) % MAX_STATES;
        
        if (buffer_[idx1].receiveTimeMs <= renderTimeMs && buffer_[idx2].receiveTimeMs >= renderTimeMs) {
            idxA = idx1;
            idxB = idx2;
            break;
        }
    }

    if (idxA < 0) {
        // renderTimeMs is outside the buffer range
        // If older than oldest state: use oldest state
        float oldestTime = buffer_[(head_ - count_ + 1 + MAX_STATES) % MAX_STATES].receiveTimeMs;
        if (renderTimeMs < oldestTime) {
            out = buffer_[(head_ - count_ + 1 + MAX_STATES) % MAX_STATES].state;
        } else {
            // Newer than newest state: use newest state (head_)
            out = buffer_[head_].state;
        }
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
        lerpAngle(snapA.state.players[i].rotY, snapB.state.players[i].rotY, t, out.players[i].rotY);
    }
}
