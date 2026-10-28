#pragma once

#include <stdint.h>

/*
 * Wire format broadcast by the ESP-NOW handheld controller.
 *
 * v2: axes are calibrated + normalized to signed -1000..1000 (0 = centered);
 *     deadzone and per-axis inversion are applied on the controller.
 */

#define CONTROLLER_PACKET_VERSION 2

#define AXIS_FULL_SCALE 1000

// Bit positions within controller_packet_t.buttons, as the controller firmware
// (esp-now-controller, components/controller_packet) assigns them. Bits 5..7 are unused.
#define BTN_LEFT (1u << 0)  // left joystick push-switch
#define BTN_RIGHT (1u << 1) // right joystick push-switch
#define BTN_A (1u << 2)     // auxiliary button A
#define BTN_B (1u << 3)     // auxiliary button B
#define BTN_C (1u << 4)     // auxiliary button C

typedef struct __attribute__((packed)) {
    uint8_t version;  // == CONTROLLER_PACKET_VERSION
    uint8_t buttons;  // bitmask of BTN_* (1 = pressed)
    int16_t left_x;   // normalized -AXIS_FULL_SCALE..+AXIS_FULL_SCALE
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
    uint32_t seq;     // monotonically increasing send counter
} controller_packet_t;

/*
 * Beacon the robot broadcasts on whatever channel its radio currently sits on.
 *
 * ESP-NOW only hears traffic on the current channel and a WiFi join retunes the
 * radio, so a fixed channel on both ends breaks the moment the robot joins a
 * router. The robot cannot tell the controller to move -- the controller would
 * have to already be listening on the robot's channel to hear that -- so the
 * discovery runs the other way: the robot announces, the controller hunts.
 *
 * Distinguishable from controller_packet_t by both length and magic.
 */

#define HEXAPOD_BEACON_MAGIC 0xA5
#define HEXAPOD_BEACON_VERSION 1
#define HEXAPOD_BEACON_PERIOD_MS 100

typedef struct __attribute__((packed)) {
    uint8_t magic;    // == HEXAPOD_BEACON_MAGIC
    uint8_t version;  // == HEXAPOD_BEACON_VERSION
    uint8_t channel;  // channel the robot is listening on right now
    uint8_t reserved; // keeps the struct 4-byte aligned; must be 0
} hexapod_beacon_t;
