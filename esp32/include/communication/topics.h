#pragma once

// The robot's server-to-client topics by tag, as platform_shared/topics.json lists them (test/host/topic_snapshot_test
// checks this table against it). A state topic holds one current value: a subscribe to it is answered with that value,
// then with the subscribe echoed as its ack, and every change follows. A stream is a sequence of frames: a subscribe to
// it is answered with the ack alone. A tag not listed is no topic, and its subscribe is acked as a stream's.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <platform_shared/message.pb.h>

class SnapshotSink;

enum class TopicKind { STATE, STREAM };

struct TopicKindEntry {
    int32_t tag;
    TopicKind kind;
};

inline constexpr TopicKindEntry TOPIC_KINDS[] = {
    {socket_message_Message_imu_tag, TopicKind::STREAM},
    {socket_message_Message_mode_tag, TopicKind::STATE},
    {socket_message_Message_analytics_tag, TopicKind::STREAM},
    {socket_message_Message_walk_gait_tag, TopicKind::STATE},
    {socket_message_Message_rssi_tag, TopicKind::STREAM},
    {socket_message_Message_telemetry_batch_tag, TopicKind::STREAM},
    {socket_message_Message_telemetry_network_tag, TopicKind::STREAM},
    {socket_message_Message_wifi_status_tag, TopicKind::STATE},
    {socket_message_Message_animation_status_tag, TopicKind::STATE},
    {socket_message_Message_sonar_tag, TopicKind::STREAM},
    {socket_message_Message_goal_status_tag, TopicKind::STATE},
    {socket_message_Message_ota_status_tag, TopicKind::STATE},
    {socket_message_Message_power_tag, TopicKind::STREAM},
    {socket_message_Message_stored_servo_settings_tag, TopicKind::STATE},
    {socket_message_Message_stored_peripheral_settings_tag, TopicKind::STATE},
    {socket_message_Message_stored_wifi_settings_tag, TopicKind::STATE},
    {socket_message_Message_stored_ap_settings_tag, TopicKind::STATE},
};

constexpr bool isStateTopic(int32_t tag) {
    for (const TopicKindEntry& entry : TOPIC_KINDS)
        if (entry.tag == tag) return entry.kind == TopicKind::STATE;
    return false;
}

// A topic as a link serves it: a state topic's snapshot reads its owner's current value and puts it into the sink,
// which sends it where the link wants it (CommAdapterBase::subscribe, push and resendDirty); a stream has none.
struct Topic {
    int32_t tag;
    TopicKind kind;
    void (*snapshot)(SnapshotSink&);
};

// Whether a table serves every topic of TOPIC_KINDS, in its order and kind, with a snapshot for each state topic and
// none for a stream.
template <size_t N>
constexpr bool servesEveryTopic(const Topic (&topics)[N]) {
    if (N != std::size(TOPIC_KINDS)) return false;
    for (size_t i = 0; i < N; i++) {
        if (topics[i].tag != TOPIC_KINDS[i].tag || topics[i].kind != TOPIC_KINDS[i].kind) return false;
        if ((topics[i].kind == TopicKind::STATE) != (topics[i].snapshot != nullptr)) return false;
    }
    return true;
}
