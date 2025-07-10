#pragma once
#include "event_bus.hpp"
#include <ArduinoJson.h>
#include <array>
#include <bitset>
#include <freertos/FreeRTOS.h>
#include "topic.hpp"

#ifndef MAX_CID
#define MAX_CID 64
#endif

enum class MsgKind : uint8_t { Connect = 0, Disconnect = 1, Event = 2, Ping = 3, Pong = 4 };

template <size_t MaxCid = MAX_CID, size_t NTopics = static_cast<size_t>(Topic::COUNT)>
class CommBase {
    using Bits = std::bitset<MaxCid>;
    std::array<Bits, NTopics> subs_;
    portMUX_TYPE mux_ portMUX_INITIALIZER_UNLOCKED;

    std::array<void*, NTopics> subscriptionHandle {};

    static constexpr size_t invalid = SIZE_MAX;

    static constexpr size_t idx(Topic t) {
        size_t i = static_cast<size_t>(t);
        return i < NTopics ? i : invalid;
    }

    template <Topic T>
    void encode(JsonDocument& d, const typename TopicTraits<T>::Msg& m) {
        auto a = d.to<JsonArray>();
        a.add(static_cast<uint8_t>(MsgKind::Event));
        a.add(static_cast<uint8_t>(T));
        toJson(a.add<JsonVariant>(), m);
    }

  protected:
    virtual void send(size_t cid, const char* data, size_t len) = 0;

    template <class Msg>
    auto& getHandle(Topic topic) {
        using H = typename EventBus<Msg>::Handle;
        static H dummy;
        auto* p = static_cast<H*>(subscriptionHandle[size_t(topic)]);
        return p ? *p : dummy;
    }

    template <class Msg>
    void setHandle(Topic topic, typename EventBus<Msg>::Handle&& h) {
        using H = typename EventBus<Msg>::Handle;
        subscriptionHandle[size_t(topic)] = new H(std::move(h));
    }

  public:
    void subscribe(Topic t, size_t cid) {
        size_t i = idx(t);
        if (i == invalid) return;
        portENTER_CRITICAL(&mux_);
        subs_[i].set(cid);
        portEXIT_CRITICAL(&mux_);
    }

    void unsubscribe(Topic t, size_t cid) {
        size_t i = idx(t);
        if (i == invalid) return;
        portENTER_CRITICAL(&mux_);
        subs_[i].reset(cid);
        portEXIT_CRITICAL(&mux_);
    }

    bool has(Topic t) const {
        size_t i = idx(t);
        return i == invalid ? false : subs_[i].any();
    }

    template <Topic T>
    void emit(const typename TopicTraits<T>::Msg& m) {
        constexpr size_t i = idx(T);
        if (i == invalid) return;
        if (!subs_[i].any()) return;

        JsonDocument doc;
        encode<T>(doc, m);
        String out;
#if USE_MSGPACK
        serializeMsgPack(doc, out);
#else
        serializeJson(doc, out);
#endif
        auto& b = subs_[i];
        for (size_t cid = 0; cid < MaxCid; ++cid)
            if (b.test(cid)) send(cid, out.c_str(), out.length());
    }
};