#pragma once
#include "msgs/imu_msg.hpp"
#include "msgs/command_msg.hpp"
#include "msgs/mode_msg.hpp"

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

// enum class Topic : uint8_t {
//     Imu = 0,
//     Command = 1,
//     Mode = 2,
//     COUNT // Should always be the last
// };

// template <Topic>
// struct TopicTraits;

// template <>
// struct TopicTraits<Topic::Imu> {
//     using Msg = ImuMsg;
//     static constexpr Topic id = Topic::Imu;
// };
// template <>
// struct TopicTraits<Topic::Command> {
//     using Msg = CommandMsg;
//     static constexpr Topic id = Topic::Command;
// };
// template <>
// struct TopicTraits<Topic::Mode> {
//     using Msg = ModeMsg;
//     static constexpr Topic id = Topic::Mode;
// };