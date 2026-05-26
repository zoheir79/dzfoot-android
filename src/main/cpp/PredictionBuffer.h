#pragma once
#include "protocol/DZFootProtocol.h"
#include <vector>
#include <cstdint>

// Buffer of sent inputs for client-side prediction + server reconciliation
class PredictionBuffer {
public:
    static constexpr size_t MAX_BUFFER = 256;

    void recordInput(uint32_t clientTick, const dzfoot::PlayerInputPacket& input);

    // When server confirms a tick, remove all inputs <= serverTick and return
    // the accumulated correction delta for the player
    void acknowledge(uint32_t serverTick, const dzfoot::NetworkPlayerState& serverState,
                     float* outPosDelta);

    void reset();

private:
    struct InputRecord {
        uint32_t tick;
        dzfoot::PlayerInputPacket input;
    };
    InputRecord buffer_[MAX_BUFFER];
    size_t head_ = 0; // write index
    size_t count_ = 0;

    float applyInputToState(const dzfoot::NetworkPlayerState& base,
                            const dzfoot::PlayerInputPacket& input);
};
