#pragma once
#include <ArduinoJson.h>
#include <array>
#include <bitset>
#include <freertos/FreeRTOS.h>
#include "topic.hpp"

#ifndef MAX_CID
#define MAX_CID 4
#endif

enum class MsgKind : uint8_t { Connect = 0, Disconnect = 1, Event = 2, Ping = 3, Pong = 4 };

template <size_t MaxCid = MAX_CID, size_t NTopics = static_cast<size_t>(Topic::COUNT)>
class CommBase {
    using Bits = std::bitset<MaxCid>;
    std::array<Bits, NTopics> subs_;
    portMUX_TYPE mux_ portMUX_INITIALIZER_UNLOCKED;

    static constexpr size_t idx(Topic t) { return static_cast<size_t>(t); }

    template <Topic T>
    void encode(JsonDocument& d, const typename TopicTraits<T>::Msg& m) {
        auto a = d.to<JsonArray>();
        a.add(static_cast<uint8_t>(MsgKind::Event));
        a.add(static_cast<uint8_t>(T));
        toJson(a.add<JsonVariant>(), m);
    }

  protected:
    virtual void send(size_t cid, const char* data, size_t len) = 0;

  public:
    void subscribe(Topic t, size_t cid) {
        portENTER_CRITICAL(&mux_);
        subs_[idx(t)].set(cid);
        portEXIT_CRITICAL(&mux_);
    }

    void unsubscribe(Topic t, size_t cid) {
        portENTER_CRITICAL(&mux_);
        subs_[idx(t)].reset(cid);
        portEXIT_CRITICAL(&mux_);
    }

    bool has(Topic t) const { return subs_[idx(t)].any(); }

    template <Topic T>
    void emit(const typename TopicTraits<T>::Msg& m) {
        if (!has(T)) return;
        JsonDocument doc;
        encode<T>(doc, m);
        String out;
#if USE_MSGPACK
        serializeMsgPack(doc, out);
#else
        serializeJson(doc, out);
#endif
        auto& b = subs_[idx(T)];
        for (size_t cid = 0; cid < MaxCid; ++cid)
            if (b.test(cid)) send(cid, out.c_str(), out.length());
    }
};