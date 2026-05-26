#include "PredictionBuffer.h"
#include <cmath>

void PredictionBuffer::recordInput(uint32_t clientTick, const dzfoot::PlayerInputPacket& input) {
    buffer_[head_].tick = clientTick;
    buffer_[head_].input = input;
    head_ = (head_ + 1) % MAX_BUFFER;
    if (count_ < MAX_BUFFER) ++count_;
}

void PredictionBuffer::reset() {
    head_ = 0;
    count_ = 0;
}

void PredictionBuffer::acknowledge(uint32_t serverTick, const dzfoot::NetworkPlayerState& serverState,
                                   float* outPosDelta) {
    outPosDelta[0] = 0.0f;
    outPosDelta[1] = 0.0f;
    outPosDelta[2] = 0.0f;

    if (count_ == 0) return;

    // Remove acknowledged inputs (all with tick <= serverTick)
    int removed = 0;
    for (size_t i = 0; i < count_; ++i) {
        size_t idx = (head_ + MAX_BUFFER - count_ + i) % MAX_BUFFER;
        if (buffer_[idx].tick <= serverTick) {
            ++removed;
        } else {
            break;
        }
    }

    if (removed > 0) {
        count_ -= removed;
    }

    // If no unacknowledged inputs remain, delta is just server - predicted
    if (count_ == 0) {
        // We don't have a locally predicted state here; caller handles smoothing
        return;
    }

    // Re-simulate from server state with remaining inputs to find delta
    // For now, return zero delta and let caller smooth toward server state
    // Full reconciliation requires physics replay — stub for later
    outPosDelta[0] = serverState.pos[0] - serverState.pos[0];
    outPosDelta[1] = serverState.pos[1] - serverState.pos[1];
    outPosDelta[2] = serverState.pos[2] - serverState.pos[2];
}
