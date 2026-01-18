#pragma once

#include <functional>
#include <list>
#include <map>
#include <type_traits>
#include <communication/proto_helpers.h>

class CommAdapterBase {
  public:
    CommAdapterBase() {
        mutex_ = xSemaphoreCreateMutex();
        decoder_.onSubscribe([this](int32_t tag, int cid) { subscribe(tag, cid); });
        decoder_.onUnsubscribe([this](int32_t tag, int cid) { unsubscribe(tag, cid); });
        decoder_.onPing([this](int cid) { sendPong(cid); });
    }
    ~CommAdapterBase() { vSemaphoreDelete(mutex_); }

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

    template <typename T>
    void emit(const T& data, int clientId = -1) {
        constexpr pb_size_t tag = MessageTraits<T>::tag;

        if (clientId < 0 && !hasSubscribers(tag)) return;

        msg_.which_message = tag;
        MessageTraits<T>::assign(msg_, data);

        size_t out_size;
        pb_get_encoded_size(&out_size, socket_message_Message_fields, &msg_);
        uint8_t *buffer = pb_heap_enc_buf;
        if (out_size > sizeof(pb_heap_enc_buf)) { // If the encoded size exceeds our buffer size, we needs to malloc a buffer of a proper size
            buffer = (uint8_t*) malloc(out_size);
        }

        pb_ostream_t stream = pb_ostream_from_buffer(buffer, out_size);
        if (!pb_encode(&stream, socket_message_Message_fields, &msg_)) {
            ESP_LOGE("ProtoComm", "Failed to encode message (tag %d), buffer too small?", (int)tag);
            return;
        }

        if (clientId >= 0) {
            send(buffer, stream.bytes_written, clientId);
        } else {
            sendToSubscribers(tag, buffer, stream.bytes_written);
        }

        if (buffer != buffer) { // If we have malloced a buffer, free it now.
            free(buffer);
        }
    }

  protected:
    virtual void send(const uint8_t* data, size_t len, int cid = -1) = 0;

    void subscribe(int32_t tag, int cid = 0) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        client_subscriptions_[tag].push_back(cid);
        xSemaphoreGive(mutex_);
        ESP_LOGI("ProtoComm", "Client %d subscribed to tag %d", cid, (int)tag);
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
        msg_.which_message = socket_message_Message_pongmsg_tag;
        msg_.message.pongmsg = socket_message_PongMsg_init_zero;
        pb_ostream_t stream = pb_ostream_from_buffer(pongBuffer, sizeof(pongBuffer));
        if (pb_encode(&stream, socket_message_Message_fields, &msg_)) {
            send(pongBuffer, stream.bytes_written, cid);
        }
    }

    SemaphoreHandle_t mutex_;
    std::map<int32_t, std::list<int>> client_subscriptions_;
    ProtoDecoder decoder_;
    socket_message_Message msg_ = socket_message_Message_init_zero;
    uint8_t pb_heap_enc_buf[PROTO_BUFFER_SIZE];

  private:
    void sendToSubscribers(int32_t tag, const uint8_t* data, size_t len) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        for (int cid : client_subscriptions_[tag]) {
            send(data, len, cid);
        }
        xSemaphoreGive(mutex_);
    }
};
