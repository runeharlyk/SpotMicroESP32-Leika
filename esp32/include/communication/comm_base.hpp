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

        pb_ostream_t stream = pb_ostream_from_buffer(buffer_, sizeof(buffer_));
        if (!pb_encode(&stream, socket_message_Message_fields, &msg_)) {
            return;
        }

        if (clientId >= 0) {
            send(buffer_, stream.bytes_written, clientId);
        } else {
            sendToSubscribers(tag, buffer_, stream.bytes_written);
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
    uint8_t buffer_[PROTO_BUFFER_SIZE];

  private:
    void sendToSubscribers(int32_t tag, const uint8_t* data, size_t len) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        for (int cid : client_subscriptions_[tag]) {
            send(data, len, cid);
        }
        xSemaphoreGive(mutex_);
    }
};
