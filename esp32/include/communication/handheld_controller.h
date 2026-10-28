#pragma once

#include <communication/controller_packet.h>
#include <platform_shared/message.pb.h>
#include <cstring>
#include <iterator>
#include <optional>

/** What one frame of the handheld controller asks of the robot, posted to the motion inbox as the app's messages are. */
struct ControllerActions {
    std::optional<socket_message_ControllerData> input;
    std::optional<socket_message_ModesEnum> mode;
    std::optional<socket_message_WalkGaits> gait;
};

/**
 * Turns the ESP-NOW handheld controller's frames into the app's messages, with Hexapod's mapping:
 *   - sticks -> the app's sticks, scaled from +-AXIS_FULL_SCALE to +-1
 *   - left button -> the next mode of DEACTIVATED, REST, STAND, WALK, and from WALK back to DEACTIVATED
 *   - right button -> toggles TROT and CRAWL, except in STAND, where while it is held the right stick's y sets the body
 *     height, which stays once it is released
 *   - both buttons -> DEACTIVATED
 * A button acts when it goes down. The controller has no sliders: the speed and the step height are the app's defaults,
 * and the height starts at the app's default.
 * Input is handed on with every frame while a stick is off centre, so the dead-man stop covers the controller as it
 * covers the app, and otherwise only when it changes, so an idle controller's heartbeat does not override the app.
 */
class HandheldController {
  public:
    static constexpr float DEFAULT_HEIGHT = 0.7f;
    static constexpr float SPEED = 0.5f;
    static constexpr float STEP_HEIGHT = 0.5f;

    /** The frame, if it is a controller packet of the version this firmware reads. */
    static std::optional<controller_packet_t> decode(const uint8_t *data, int len) {
        if (len != static_cast<int>(sizeof(controller_packet_t))) return std::nullopt;
        controller_packet_t packet;
        std::memcpy(&packet, data, sizeof(packet));
        if (packet.version != CONTROLLER_PACKET_VERSION) return std::nullopt;
        return packet;
    }

    /** What the frame asks for, given the mode and gait the robot is in. */
    ControllerActions handle(const controller_packet_t &packet, socket_message_ModesEnum mode,
                             socket_message_WalkGaits gait) {
        ControllerActions actions;
        const bool inStand = mode == socket_message_ModesEnum_STAND;

        socket_message_ControllerData input = sticks(packet);
        if (inStand && (packet.buttons & BTN_RIGHT)) {
            height = heightFromStick(input.right.y);
            input.right.y = 0;
        }
        input.height = height;
        input.speed = SPEED;
        input.s1 = STEP_HEIGHT;
        if (steering(input) || !posted || !sameInput(input, *posted)) actions.input = posted = input;

        const uint8_t rising = packet.buttons & ~previousButtons;
        previousButtons = packet.buttons;

        if ((packet.buttons & (BTN_LEFT | BTN_RIGHT)) == (BTN_LEFT | BTN_RIGHT)) {
            if (rising) actions.mode = socket_message_ModesEnum_DEACTIVATED;
            return actions;
        }
        if (rising & BTN_LEFT) actions.mode = nextMode(mode);
        if ((rising & BTN_RIGHT) && !inStand)
            actions.gait = gait == socket_message_WalkGaits_TROT ? socket_message_WalkGaits_CRAWL
                                                                 : socket_message_WalkGaits_TROT;
        return actions;
    }

  private:
    static constexpr socket_message_ModesEnum MODE_CYCLE[] = {
        socket_message_ModesEnum_DEACTIVATED, socket_message_ModesEnum_REST, socket_message_ModesEnum_STAND,
        socket_message_ModesEnum_WALK};

    static socket_message_ControllerData sticks(const controller_packet_t &packet) {
        constexpr float k = 1.0f / AXIS_FULL_SCALE;
        socket_message_ControllerData input = socket_message_ControllerData_init_zero;
        input.has_left = input.has_right = true;
        input.left.x = packet.left_x * k;
        input.left.y = packet.left_y * k;
        input.right.x = packet.right_x * k;
        input.right.y = packet.right_y * k;
        return input;
    }

    // Centred is the default height, and the stick's ends are the lowest and the highest.
    static float heightFromStick(float y) {
        return DEFAULT_HEIGHT + y * (y > 0 ? 1.0f - DEFAULT_HEIGHT : DEFAULT_HEIGHT);
    }

    static bool steering(const socket_message_ControllerData &input) {
        return input.left.x != 0 || input.left.y != 0 || input.right.x != 0 || input.right.y != 0;
    }

    static bool sameInput(const socket_message_ControllerData &a, const socket_message_ControllerData &b) {
        return a.left.x == b.left.x && a.left.y == b.left.y && a.right.x == b.right.x && a.right.y == b.right.y &&
               a.height == b.height && a.speed == b.speed && a.s1 == b.s1;
    }

    // Any mode outside the cycle (IDLE, CALIBRATION) steps on as DEACTIVATED does.
    static socket_message_ModesEnum nextMode(socket_message_ModesEnum mode) {
        size_t index = 0;
        for (size_t i = 0; i < std::size(MODE_CYCLE); i++)
            if (MODE_CYCLE[i] == mode) index = i;
        return MODE_CYCLE[(index + 1) % std::size(MODE_CYCLE)];
    }

    uint8_t previousButtons = 0;
    float height = DEFAULT_HEIGHT;
    std::optional<socket_message_ControllerData> posted;
};

/**
 * Pairs the robot with the first controller it hears: ESP-NOW broadcasts carry no pairing, so without this any
 * controller in range would drive the robot. Only a valid controller frame claims the robot, so another ESP-NOW device
 * nearby cannot; a different controller is taken after the receiver is switched off and on again.
 */
class ControllerLock {
  public:
    /** Whether a frame from `mac` may drive the robot; the first valid frame since the last reset claims it. */
    bool admit(const uint8_t (&mac)[6], const uint8_t *data, int len) {
        if (!_locked) {
            if (!HandheldController::decode(data, len)) return false;
            std::memcpy(_mac, mac, sizeof(_mac));
            _locked = true;
            return true;
        }
        return std::memcmp(_mac, mac, sizeof(_mac)) == 0;
    }

    void reset() { _locked = false; }

  private:
    bool _locked = false;
    uint8_t _mac[6] {};
};

/** The beacon announcing the channel the robot's radio is on, for the controller to find it. */
inline hexapod_beacon_t beaconFor(uint8_t channel) {
    return {HEXAPOD_BEACON_MAGIC, HEXAPOD_BEACON_VERSION, channel, 0};
}
