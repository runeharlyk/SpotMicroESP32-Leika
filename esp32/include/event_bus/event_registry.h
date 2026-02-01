#pragma once
#include <event_bus/event_types.h>
#include <platform_shared/api.pb.h>
#include <platform_shared/message.pb.h>

template <typename T>
struct EventTypeTraits;

template <typename T>
struct EventBusConfig {
    static constexpr size_t QueueDepth = 64;
    static constexpr size_t MaxSubs = 8;
    static constexpr size_t BatchSize = 16;
};

#define REGISTER_EVENT_TYPE(MsgType, EventTypeValue)      \
    template <>                                           \
    struct EventTypeTraits<MsgType> {                     \
        static constexpr EventType type = EventTypeValue; \
        static constexpr const char* name = #MsgType;     \
    };

#define REGISTER_SETTINGS_TYPE(MsgType, EventTypeValue) \
    REGISTER_EVENT_TYPE(MsgType, EventTypeValue)        \
    template <>                                         \
    struct EventBusConfig<MsgType> {                    \
        static constexpr size_t QueueDepth = 1;         \
        static constexpr size_t MaxSubs = 6;            \
        static constexpr size_t BatchSize = 1;          \
    };

REGISTER_SETTINGS_TYPE(api_WifiSettings, EventType::WIFI_SETTINGS)
REGISTER_SETTINGS_TYPE(api_APSettings, EventType::AP_SETTINGS)
REGISTER_SETTINGS_TYPE(api_MDNSSettings, EventType::MDNS_SETTINGS)
REGISTER_SETTINGS_TYPE(api_PeripheralSettings, EventType::PERIPHERAL_SETTINGS)
REGISTER_SETTINGS_TYPE(api_ServoSettings, EventType::SERVO_SETTINGS)
REGISTER_SETTINGS_TYPE(api_CameraSettings, EventType::CAMERA_SETTINGS)

#define REGISTER_COMMAND_TYPE(MsgType, EventTypeValue) \
    REGISTER_EVENT_TYPE(MsgType, EventTypeValue)       \
    template <>                                        \
    struct EventBusConfig<MsgType> {                   \
        static constexpr size_t QueueDepth = 1;        \
        static constexpr size_t MaxSubs = 3;           \
        static constexpr size_t BatchSize = 1;         \
    };

#define REGISTER_STREAM_TYPE(MsgType, EventTypeValue) \
    REGISTER_EVENT_TYPE(MsgType, EventTypeValue)      \
    template <>                                       \
    struct EventBusConfig<MsgType> {                  \
        static constexpr size_t QueueDepth = 4;       \
        static constexpr size_t MaxSubs = 3;          \
        static constexpr size_t BatchSize = 4;        \
    };

REGISTER_STREAM_TYPE(socket_message_IMUData, EventType::IMU_DATA)
REGISTER_COMMAND_TYPE(socket_message_ControllerData, EventType::MOTION_COMMAND)
REGISTER_COMMAND_TYPE(socket_message_ModeData, EventType::MOTION_MODE)
REGISTER_COMMAND_TYPE(socket_message_AnglesData, EventType::MOTION_ANGLES)
REGISTER_COMMAND_TYPE(socket_message_WalkGaitData, EventType::MOTION_WALK_GAIT)
REGISTER_COMMAND_TYPE(socket_message_ServoStateData, EventType::SERVO_STATE)
REGISTER_COMMAND_TYPE(socket_message_ServoPWMData, EventType::SERVO_PWM)
