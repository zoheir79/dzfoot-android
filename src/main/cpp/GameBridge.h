#pragma once
#include <cstdint>
#include <vector>
#include "protocol/DZFootProtocol.h"

class GameBridge {
public:
    GameBridge();
    void applyGameState(const uint8_t* data, size_t len);
    void applyMatchEvent(const uint8_t* data, size_t len);

    const dzfoot::GameStatePacket& currentState() const { return state_; }
    std::vector<dzfoot::MatchEventPacket> flushEvents();

    void setARCamera(const float* viewMatrix, const float* projMatrix, const float* anchorMatrix);

private:
    dzfoot::GameStatePacket state_;
    std::vector<dzfoot::MatchEventPacket> pendingEvents_;
    float arViewMatrix_[16];
    float arProjMatrix_[16];
    float arAnchorMatrix_[16];
    bool useARCamera_ = false;
};
