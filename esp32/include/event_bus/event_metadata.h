#pragma once
#include <event_bus/event_types.h>
#include <cstdint>

struct EventMetadata {
    EventType type;
    uint64_t timestamp;
    uint32_t sequence;
    const char* source;
};

template <typename Msg>
struct EventEnvelope {
    EventMetadata metadata;
    Msg payload;
};
