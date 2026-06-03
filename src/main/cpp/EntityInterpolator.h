#pragma once
#include "protocol/DZFootProtocol.h"
#include <vector>
#include <cstring>

// Ring buffer of GameStates for entity interpolation
// Displays remote entities ~100ms behind for smooth 20 Hz updates
class EntityInterpolator {
public:
    static constexpr int MAX_STATES = 8;
    static constexpr float INTERP_DELAY_MS = 100.0f; // ~100ms behind

    void addState(const dzfoot::GameStatePacket& state);
    void clear() { head_ = 0; count_ = 0; }

    // Interpolate all player positions and ball into 'out' at 'renderTimeMs' (current time - delay)
    void interpolate(float renderTimeMs, dzfoot::GameStatePacket& out) const;

    float latestReceivedTimeMs() const;

private:
    struct StateSnapshot {
        dzfoot::GameStatePacket state;
        float receiveTimeMs;
    };
    StateSnapshot buffer_[MAX_STATES];
    int head_ = 0; // index of most recent
    int count_ = 0;

    float getTimeMs(const dzfoot::GameStatePacket& s) const;
    static void lerpVec3(const float* a, const float* b, float t, float* out);
    static void lerpFloat(float a, float b, float t, float& out);
    static void lerpAngle(float a, float b, float t, float& out);
};
