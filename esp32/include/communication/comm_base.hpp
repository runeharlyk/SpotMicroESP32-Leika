#pragma once

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <algorithm>
#include <list>
#include <map>
#include <type_traits>
#include <communication/proto_helpers.h>

class CommAdapterBase {
  public:
    CommAdapterBase() {
        mutex_ = xSemaphoreCreateMutex();
        encode_mutex_ = xSemaphoreCreateMutex();
        decoder_.onSubscribe([this](int32_t tag, int cid) { subscribe(tag, cid); });
        decoder_.onUnsubscribe([this](int32_t tag, int cid) { unsubscribe(tag, cid); });
        decoder_.onPing([this](int cid) { sendPong(cid); });
    }
    ~CommAdapterBase() {
        vSemaphoreDelete(mutex_);
        vSemaphoreDelete(encode_mutex_);
    }

    virtual void begin() {}

    bool hasSubscribers(int32_t tag) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        bool result = !client_subscriptions_[tag].empty();
        xSemaphoreGive(mutex_);
        return result;
    }

    ProtoDecoder& decoder() { return decoder_; }

    template <typename T>
    void on(std::function<void(const T&, int)> handler) {
        decoder_.on<T>(handler);
    }

    /** Whether every addressed client got the message. */
    template <typename T>
    bool emit(const T& data, int clientId = -1) {
        constexpr pb_size_t tag = MessageTraits<T>::tag;

        if (clientId < 0 && !hasSubscribers(tag)) return true;

        // Tasks other than the socket's emit too (telemetry, deferred replies); msg_ and the
        // encode buffer are shared.
        xSemaphoreTake(encode_mutex_, portMAX_DELAY);
        msg_.which_message = tag;
        MessageTraits<T>::assign(msg_, data);

        size_t out_size;
        pb_get_encoded_size(&out_size, socket_message_Message_fields, &msg_);
        uint8_t* buffer = pb_heap_enc_buf;
        if (out_size > sizeof(pb_heap_enc_buf)) { // If the encoded size exceeds our buffer size, we needs to malloc a
                                                  // buffer of a proper size
            buffer = (uint8_t*)malloc(out_size);
            if (!buffer) {
                ESP_LOGE("ProtoComm", "No memory to encode message (tag %d, %u bytes)", (int)tag, out_size);
                xSemaphoreGive(encode_mutex_);
                return false;
            }
        }

        bool sent = false;
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, out_size);
        if (!pb_encode(&stream, socket_message_Message_fields, &msg_)) {
            ESP_LOGE("ProtoComm", "Failed to encode message (tag %d), buffer too small?", (int)tag);
        } else if (clientId >= 0) {
            sent = send(buffer, stream.bytes_written, clientId);
        } else {
            sent = sendToSubscribers(tag, buffer, stream.bytes_written);
        }

        if (pb_heap_enc_buf != buffer) {
            free(buffer);
        }
        xSemaphoreGive(encode_mutex_);
        return sent;
    }

    /** Called on the socket's task after a client subscribes to a tag. */
    void onSubscribed(std::function<void(int32_t tag, int cid)> listener) { subscribedListener_ = std::move(listener); }

  protected:
    virtual bool send(const uint8_t* data, size_t len, int cid) = 0;

    /** Whether a client can take a frame now, without waiting. */
    virtual bool ready(int cid) { return true; }

    void subscribe(int32_t tag, int cid = 0) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        std::list<int>& clients = client_subscriptions_[tag];
        if (std::find(clients.begin(), clients.end(), cid) == clients.end()) clients.push_back(cid);
        xSemaphoreGive(mutex_);
        ESP_LOGI("ProtoComm", "Client %d subscribed to tag %d", cid, (int)tag);
        if (subscribedListener_) subscribedListener_(tag, cid);
    }

    void unsubscribe(int32_t tag, int cid = 0) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        client_subscriptions_[tag].remove(cid);
        xSemaphoreGive(mutex_);
        ESP_LOGI("ProtoComm", "Client %d unsubscribed from tag %d", cid, (int)tag);
    }

    void removeClient(int cid) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        for (auto& [tag, clients] : client_subscriptions_) {
            clients.remove(cid);
        }
        xSemaphoreGive(mutex_);
    }

    void handleIncoming(const uint8_t* data, size_t len, int cid) {
        if (!decoder_.decode(data, len, cid)) {
            ESP_LOGE("ProtoComm", "Failed to decode incoming message from client %d", cid);
        }
    }

    void sendPong(int cid) {
        uint8_t pongBuffer[16];
        xSemaphoreTake(encode_mutex_, portMAX_DELAY);
        msg_.which_message = socket_message_Message_pongmsg_tag;
        msg_.message.pongmsg = socket_message_PongMsg_init_zero;
        pb_ostream_t stream = pb_ostream_from_buffer(pongBuffer, sizeof(pongBuffer));
        if (pb_encode(&stream, socket_message_Message_fields, &msg_)) {
            send(pongBuffer, stream.bytes_written, cid);
        }
        xSemaphoreGive(encode_mutex_);
    }

    SemaphoreHandle_t mutex_;
    SemaphoreHandle_t encode_mutex_;
    std::map<int32_t, std::list<int>> client_subscriptions_;
    ProtoDecoder decoder_;
    std::function<void(int32_t, int)> subscribedListener_;
    socket_message_Message msg_ = socket_message_Message_init_zero;
    uint8_t pb_heap_enc_buf[PROTO_BUFFER_SIZE];

  private:
    // Sends to a copy of the list, so a slow client never holds up subscribing or closing on the socket task.
    // Subscriptions stream the latest value, so a client that cannot take one now misses it: queueing it
    // instead held memory for every frame a slow link had not yet sent, until the heap ran out.
    bool sendToSubscribers(int32_t tag, const uint8_t* data, size_t len) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const std::list<int> clients = client_subscriptions_[tag];
        xSemaphoreGive(mutex_);
        bool sent = true;
        for (int cid : clients) {
            sent = ready(cid) && send(data, len, cid) && sent;
        }
        return sent;
    }
};
