#pragma once
#include "msgs/imu_msg.hpp"
#include "msgs/command_msg.hpp"
#include "msgs/mode_msg.hpp"
#include "msgs/servo_msg.hpp"
#include "msgs/motion_input_msg.hpp"
#include "msgs/motion_angles_msg.hpp"
#include "msgs/motion_position_msg.hpp"
#include "msgs/motion_mode_msg.hpp"
#include "msgs/topics.def"

enum class Topic : uint8_t {
#define X(e, t) e,
    TOPIC_LIST
#undef X
        COUNT
};

template <Topic>
struct TopicTraits;

#define X(e, t)                    \
    template <>                    \
    struct TopicTraits<Topic::e> { \
        using Msg = t;             \
    };
TOPIC_LIST
#undef X

template <typename Msg>
static Msg parse(JsonVariantConst v) {
    Msg msg;
    msg.fromJson(v);
    return msg;
}
