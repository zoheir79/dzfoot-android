#include "NetworkClient.h"

bool NetworkClient::init(const char* wsUrl, const char* token) {
    // TODO: integrate LiveKit C++ SDK or JNI bridge
    // For now this is a stub; real impl uses LiveKit client-sdk-cpp
    return true;
}

void NetworkClient::destroy() {}

void NetworkClient::sendInput(const uint8_t* data, size_t len) {
    // TODO: publishData via LiveKit C++ SDK topic "in"
}

void NetworkClient::setGameStateCallback(GameStateCallback cb) {
    gsCb_ = cb;
}

void NetworkClient::setEventCallback(EventCallback cb) {
    evCb_ = cb;
}
