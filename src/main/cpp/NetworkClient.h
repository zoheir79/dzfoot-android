#pragma once
#include <cstdint>
#include <vector>
#include <functional>

class NetworkClient {
public:
    using GameStateCallback = std::function<void(const uint8_t* data, size_t len)>;
    using EventCallback     = std::function<void(const uint8_t* data, size_t len)>;

    bool init(const char* wsUrl, const char* token);
    void destroy();

    void sendInput(const uint8_t* data, size_t len);
    void setGameStateCallback(GameStateCallback cb);
    void setEventCallback(EventCallback cb);

private:
    GameStateCallback gsCb_;
    EventCallback evCb_;
};
