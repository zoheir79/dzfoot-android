#include <android/log.h>
#include <cstring>
#include <limits>
#include "protocol/DZFootProtocol.h"

#define LOG_TAG "ProtocolTest"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool runProtocolTests() {
    using namespace dzfoot;

    LOGI("DZFoot Protocol v%d layout tests (Android)", DZ_PROTOCOL_VERSION);
    LOGI("sizeof(PacketHeader)       = %zu", sizeof(PacketHeader));
    LOGI("sizeof(NetworkBallState)   = %zu", sizeof(NetworkBallState));
    LOGI("sizeof(NetworkPlayerState) = %zu", sizeof(NetworkPlayerState));
    LOGI("sizeof(GameStatePacket)    = %zu", sizeof(GameStatePacket));
    LOGI("sizeof(MatchEventPacket)   = %zu", sizeof(MatchEventPacket));
    LOGI("sizeof(PlayerInputPacket)  = %zu", sizeof(PlayerInputPacket));

    // Build golden packet matching server test
    GameStatePacket gs{};
    gs.header.magic = DZ_MAGIC;
    gs.header.version = DZ_PROTOCOL_VERSION;
    gs.header.type = PACKET_GAME_STATE;
    gs.header.size = sizeof(gs);
    gs.header.flags = 0;
    gs.tick = 12345;
    gs.timestampUs = 9876543210ULL;
    gs.gameMode = 1;
    gs.gameFlags = 0;
    gs.score[0] = 2;
    gs.score[1] = 1;
    gs.timer = 42.5f;
    gs.ball.pos[0] = 1.0f; gs.ball.pos[1] = 0.2f; gs.ball.pos[2] = -0.5f;
    gs.ball.vel[0] = 0.1f; gs.ball.vel[1] = 0.0f; gs.ball.vel[2] = 0.05f;
    gs.ball.ownedTeam = 0;
    gs.ball.ownedPlayer = 3;
    for (int i = 0; i < DZ_MAX_PLAYERS; ++i) {
        gs.players[i].pos[0] = float(i);
        gs.players[i].pos[1] = float(i) * 0.1f;
        gs.players[i].pos[2] = -float(i);
        gs.players[i].anim = static_cast<uint8_t>(i % ANIM_COUNT);
        gs.players[i].team = (i < 11) ? 0 : 1;
    }

    uint8_t buf[sizeof(gs)];
    std::memcpy(buf, &gs, sizeof(gs));

    if (!validateGameStatePacket(buf, sizeof(buf))) {
        LOGE("FAIL: valid packet rejected");
        return false;
    }
    if (validateGameStatePacket(buf, sizeof(buf) - 1)) {
        LOGE("FAIL: truncated packet accepted");
        return false;
    }
    buf[4] = 99; // corrupt version
    if (validateGameStatePacket(buf, sizeof(buf))) {
        LOGE("FAIL: bad version accepted");
        return false;
    }
    buf[4] = 1; // restore
    if (!validateGameStatePacket(buf, sizeof(buf))) {
        LOGE("FAIL: restored packet rejected");
        return false;
    }

    GameStatePacket gsNaN = gs;
    gsNaN.players[0].pos[0] = std::numeric_limits<float>::quiet_NaN();
    if (isFiniteVec3(gsNaN.players[0].pos)) {
        LOGE("FAIL: NaN not detected");
        return false;
    }

    LOGI("All protocol tests PASSED");
    return true;
}
