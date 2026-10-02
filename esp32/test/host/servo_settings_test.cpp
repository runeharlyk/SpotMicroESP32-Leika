// Host test of settings/servo_settings.h, built with nanopb and api.pb.c by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <pb_decode.h>
#include <settings/servo_settings.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static ServoSettings withChannels(std::vector<uint32_t> channels) {
    ServoSettings settings = ServoSettings_defaults();
    settings.channels_count = channels.size();
    for (size_t i = 0; i < channels.size(); i++) settings.channels[i] = channels[i];
    return settings;
}

static void theDefaultsWireJointToChannelInOrder() {
    const ServoSettings settings = ServoSettings_defaults();
    CHECK(validServoSettings(settings));
    for (size_t joint = 0; joint < 12; joint++) CHECK(jointChannel(settings, joint) == joint);
}

static void aChannelMapRoutesEachJoint() {
    const ServoSettings settings = withChannels({15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4});
    CHECK(validServoSettings(settings));
    CHECK(jointChannel(settings, 0) == 15);
    CHECK(jointChannel(settings, 11) == 4);
}

static void aBrokenChannelMapIsRefused() {
    ServoSettings current = ServoSettings_defaults();
    CHECK(ServoSettings_update(withChannels({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10}), current) == StateUpdateResult::ERROR);
    CHECK(ServoSettings_update(withChannels({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 16}), current) == StateUpdateResult::ERROR);
    CHECK(ServoSettings_update(withChannels({0, 1, 2}), current) == StateUpdateResult::ERROR);
    CHECK(current.channels_count == 0);
}

static void aNonFiniteOrOutOfRangeCentreIsRefused() {
    ServoSettings proto = ServoSettings_defaults();
    proto.servos[3].center_pwm = NAN;
    ServoSettings current = ServoSettings_defaults();
    CHECK(ServoSettings_update(proto, current) == StateUpdateResult::ERROR);
    proto = ServoSettings_defaults();
    proto.servos[3].center_pwm = 900;
    CHECK(ServoSettings_update(proto, current) == StateUpdateResult::ERROR);
    proto = ServoSettings_defaults();
    proto.servos_count = 11;
    CHECK(ServoSettings_update(proto, current) == StateUpdateResult::ERROR);
}

static void aSaveWithoutChannelsKeepsTheWiring() {
    ServoSettings current = withChannels({15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4});
    ServoSettings save = ServoSettings_defaults();  // an app that never sends channels
    save.servos[0].center_pwm = 280;
    CHECK(ServoSettings_update(save, current) == StateUpdateResult::CHANGED);
    CHECK(current.servos[0].center_pwm == 280);
    CHECK(jointChannel(current, 0) == 15);
}

static void theReplyReportsTheVariantsModel() {
    ServoSettings reply = api_ServoSettings_init_zero;
    ServoSettings_read(ServoSettings_defaults(), reply);
    CHECK(reply.has_model);
    CHECK(reply.model.direction[0] == VARIANT_JOINT_MODEL.direction[0]);
    CHECK(reply.model.center_angle[2] == VARIANT_JOINT_MODEL.center_angle[2]);
    CHECK(reply.model.pwm_per_degree == VARIANT_JOINT_MODEL.pwm_per_degree);
}

// What today's firmware stores: per servo the centre PWM (1), direction (2), centre angle (3), conversion (4) and
// name (5), all fixed32 floats except the name. Built by hand, since the new proto no longer has those fields.
static std::vector<uint8_t> todaysFile() {
    std::vector<uint8_t> file;
    auto put32 = [](std::vector<uint8_t> &out, float value) {
        uint8_t bytes[4];
        std::memcpy(bytes, &value, 4);
        out.insert(out.end(), bytes, bytes + 4);
    };
    for (int joint = 0; joint < 12; joint++) {
        std::vector<uint8_t> servo;
        servo.push_back(0x0D); put32(servo, 250.0f + joint);  // center_pwm
        servo.push_back(0x15); put32(servo, -1.0f);           // direction
        servo.push_back(0x1D); put32(servo, 45.0f);           // center_angle
        servo.push_back(0x25); put32(servo, 2.0f);            // conversion
        servo.push_back(0x2A); servo.push_back(2); servo.push_back('S'); servo.push_back('x');  // name
        file.push_back(0x0A);
        file.push_back(static_cast<uint8_t>(servo.size()));
        file.insert(file.end(), servo.begin(), servo.end());
    }
    return file;
}

static void aTodaysSettingsFileKeepsItsCentres() {
    const std::vector<uint8_t> file = todaysFile();
    ServoSettings decoded = api_ServoSettings_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(file.data(), file.size());
    CHECK(pb_decode(&stream, api_ServoSettings_fields, &decoded));
    ServoSettings current = ServoSettings_defaults();
    CHECK(ServoSettings_update(decoded, current) == StateUpdateResult::CHANGED);
    CHECK(current.servos[0].center_pwm == 250);
    CHECK(current.servos[11].center_pwm == 261);
    CHECK(jointChannel(current, 7) == 7);
}

int main() {
    theDefaultsWireJointToChannelInOrder();
    aChannelMapRoutesEachJoint();
    aBrokenChannelMapIsRefused();
    aNonFiniteOrOutOfRangeCentreIsRefused();
    aSaveWithoutChannelsKeepsTheWiring();
    theReplyReportsTheVariantsModel();
    aTodaysSettingsFileKeepsItsCentres();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
