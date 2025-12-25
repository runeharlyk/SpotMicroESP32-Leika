#pragma once

#include <ArduinoJson.h>
#include <functional>

enum message_type_t { CONNECT = 0, DISCONNECT = 1, EVENT = 2, PING = 3, PONG = 4, BINARY_EVENT = 5 };

typedef std::function<void(JsonVariant &root, int originId)> EventCallback;
typedef std::function<void(const std::string &originId, bool sync)> SubscribeCallback;

class CommAdapterBase {
  public:
    CommAdapterBase() { mutex_ = xSemaphoreCreateMutex(); }
    ~CommAdapterBase() { vSemaphoreDelete(mutex_); }

    virtual void begin() {}

    bool hasSubscribers(const char *event) { return !client_subscriptions[event].empty(); }

    void onEvent(std::string event, EventCallback callback) { event_callbacks[event].push_back(std::move(callback)); }

    void onSubscribe(std::string event, SubscribeCallback callback) {
        subscribe_callbacks[event].push_back(std::move(callback));
    }

    void emit(const char *event, JsonVariant &payload, const char *originId = "", bool onlyToSameOrigin = false) {
        int originSubscriptionId = originId[0] ? atoi(originId) : -1;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        auto &subscriptions = client_subscriptions[event];
        if (subscriptions.empty()) {
            xSemaphoreGive(mutex_);
            return;
        }

        JsonDocument doc;
        JsonArray array = doc.to<JsonArray>();
        array.add(static_cast<uint8_t>(message_type_t::EVENT));
        array.add(event);
        array.add(payload);

#if USE_MSGPACK
        std::string bin;
        serializeMsgPack(doc, bin);
        xSemaphoreGive(mutex_);
        send(reinterpret_cast<const uint8_t *>(bin.data()), bin.size(), -1);
#else
        String out;
        serializeJson(doc, out);
        xSemaphoreGive(mutex_);
        send(out.c_str(), -1);
#endif
    }

  protected:
    void send(const char *data, int cid = -1) { send(reinterpret_cast<const uint8_t *>(data), strlen(data), cid); }
    virtual void send(const uint8_t *data, size_t len, int cid = -1) = 0;

    void subscribe(const char *event, int cid = 0) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        client_subscriptions[event].push_back(cid);
        xSemaphoreGive(mutex_);
    }
    void unsubscribe(const char *event, int cid = 0) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        client_subscriptions[event].remove(cid);
        xSemaphoreGive(mutex_);
    }

    void handleEventCallbacks(std::string event, JsonVariant &jsonObject, int originId) {
        for (auto &callback : event_callbacks[event]) {
            callback(jsonObject, originId);
        }
    }

    virtual void handleIncoming(const uint8_t *data, size_t len, int cid = 0) {
        JsonDocument doc;
#if USE_MSGPACK
        DeserializationError error = deserializeMsgPack(doc, data, len);
#else
        DeserializationError error = deserializeJson(doc, data, len);
#endif
        if (error) {
            ESP_LOGE("Comm Base", "Failed to deserialize incoming: (%s)", error.c_str());
            return;
        }

        JsonArray obj = doc.as<JsonArray>(); // TODO: Make const
        message_type_t type = static_cast<message_type_t>(obj[0].as<uint8_t>());

        switch (type) {
            case message_type_t::CONNECT: {
                const char *event = obj[1].as<const char *>();
                ESP_LOGI("Comm Base", "CONNECT topic: %s (cid=%d)", event, cid);
                subscribe(event, cid);
                break;
            }

            case message_type_t::DISCONNECT: {
                const char *event = obj[1].as<const char *>();
                ESP_LOGI("Comm Base", "DISCONNECT topic: %s (cid=%d)", event, cid);
                unsubscribe(event, cid);
                break;
            }

            case message_type_t::EVENT: {
                const char *event = obj[1].as<const char *>();
                JsonVariant payload = obj[2].as<JsonVariant>();
                handleEventCallbacks(event, payload, cid);
                break;
            }
            case message_type_t::PING: {
                ESP_LOGI("Comm Base", "PING (cid=%d)", cid);
#if USE_MSGPACK
                static const uint8_t pong[] = {0x91, 0x04};
                send(pong, sizeof(pong), cid);
#else
                send("[4]", cid);
#endif
                break;
            }
            case message_type_t::PONG: ESP_LOGI("Comm Base", "PONG (cid=%d)", cid); break;
            default: ESP_LOGW("Comm Base", "Unknown message type: %d", static_cast<int>(type)); break;
        }

        if (type == PONG) {
            ESP_LOGV("EventSocket", "Pong");
            return;
        } else if (type == PING) {
            ESP_LOGV("EventSocket", "Ping");
            ping(cid);
            return;
        }
    }

    void ping(int cid) {
#if USE_MSGPACK
        const uint8_t out[] = {0x91, 0x04};
        send(out, sizeof(out), cid);
#else
        const char *out = "[4]";
        send(out, strlen(out), cid);
#endif
    }

    SemaphoreHandle_t mutex_;
    std::map<std::string, std::list<int>> client_subscriptions;
    std::map<std::string, std::list<EventCallback>> event_callbacks;
    std::map<std::string, std::list<SubscribeCallback>> subscribe_callbacks;
};