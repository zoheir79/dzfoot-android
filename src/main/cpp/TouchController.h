#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <mutex>
#include "protocol/DZFootProtocol.h"

// Multi-touch virtual controller: joystick (left), action buttons (right), radar (center)
class TouchController {
public:
    void setScreenSize(int width, int height);

    // Multi-touch handlers
    void onTouchDown(int pointerId, float x, float y);
    void onTouchMove(int pointerId, float x, float y);
    void onTouchUp(int pointerId);

    // Action button state setters (called from Java buttons or touch zones)
    void setActionShot(bool on);
    void setActionPass(bool on);
    void setActionKick(bool on);
    void setActionDribble(bool on);
    void setActionHighPass(bool on);
    void setActionSliding(bool on);
    void setActionSwitchPlayer(bool on);
    void setSprint(bool on);

    // Set which player this input controls (from GameState flags & 0x04)
    void setActivePlayer(uint8_t team, uint8_t playerIdx);

    // Rotate raw joystick input by camera forward on XZ plane.
    // camFwdX/Z = camera look direction (will be normalized internally).
    void applyCameraRotation(float camFwdX, float camFwdZ);

    const dzfoot::PlayerInputPacket& getInput() const { return input_; }
    void serialize(uint8_t* out, size_t maxLen) const;

    // UI query — used by renderer to draw overlays
    struct JoystickState {
        float cx, cy;      // center in pixels
        float stickX, stickY; // offset [-1,1]
        float radius;
        bool active;
    };
    struct ButtonState {
        const char* label;
        float cx, cy, radius;
        bool pressed;
        uint32_t color; // RGBA
        float power;    // 0..1 charge gauge
    };
    static constexpr int MAX_ACTION_BUTTONS = 6;
    JoystickState getJoystickState() const;
    int getActionButtons(ButtonState out[MAX_ACTION_BUTTONS]) const;

    // Get current charge power for a specific button mask (SHOT, PASS, etc.)
    float getChargePower(uint16_t buttonMask) const;

    // Radar data
    struct RadarDot {
        float nx, ny; // normalized [-1,1] on pitch
        uint32_t color;
        bool isBall;
    };
    static constexpr int MAX_RADAR_DOTS = 25;

    // Directional action indicator (pass/shot aim)
    struct AimIndicator {
        bool visible;
        float originX, originY; // world coords
        float dirX, dirY;
        float power; // 0..1
        uint32_t color;
    };

    // Update radar from current game state
    void updateRadar(const dzfoot::GameStatePacket& gs);
    int getRadarDots(RadarDot out[MAX_RADAR_DOTS]) const;
    AimIndicator getAimIndicator() const { return aim_; }

private:
    void updateJoystick(int pointerId, float x, float y);
    void updateButtonStates();
    void updateButtonStatesInternal();

    int screenW_ = 1080, screenH_ = 1920;
    dzfoot::PlayerInputPacket input_ = {};

    // Multi-touch tracking
    static constexpr int MAX_POINTERS = 10;
    struct Pointer {
        bool active = false;
        float x = 0, y = 0;
    };
    Pointer pointers_[MAX_POINTERS];

    // Zones (computed from screen size)
    float joyCx_ = 150, joyCy_ = 1600, joyRadius_ = 120;
    float joyStickX_ = 0, joyStickY_ = 0;
    bool joyActive_ = false;
    int joyPointer_ = -1;

    struct ActionBtn {
        const char* label;
        float cx, cy, radius;
        uint16_t buttonMask;
        uint32_t color;
        bool pressed;
        int pointerId;
        double pressTimeMs; // timestamp when button was pressed
    };
    ActionBtn buttons_[MAX_ACTION_BUTTONS];
    int numButtons_ = 0;
    bool buttonsInitialized_ = false;

    // Radar
    RadarDot radarDots_[MAX_RADAR_DOTS];
    int numRadarDots_ = 0;

    // Active player (set from GameState flags & 0x04)
    uint8_t activeTeam_ = 0;
    uint8_t activePlayerIdx_ = 0;

    // Aim
    AimIndicator aim_ = {};

    mutable std::recursive_mutex mutex_;
};
