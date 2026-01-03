#pragma once

#include <functional>
#include <list>
#include <map>
#include <type_traits>
#include <communication/proto_helpers.h>

template <typename T>
struct MessageTraits;

template <>
struct MessageTraits<socket_message_IMUData> {
    static constexpr pb_size_t tag = socket_message_WebsocketMessage_imu_tag;
    static void assign(socket_message_WebsocketMessage& msg, const socket_message_IMUData& data) {
        msg.message.imu = data;
    }
};

template <>
struct MessageTraits<socket_message_ModeData> {
    static constexpr pb_size_t tag = socket_message_WebsocketMessage_mode_tag;
    static void assign(socket_message_WebsocketMessage& msg, const socket_message_ModeData& data) {
        msg.message.mode = data;
    }
};

template <>
struct MessageTraits<socket_message_AnalyticsData> {
    static constexpr pb_size_t tag = socket_message_WebsocketMessage_analytics_tag;
    static void assign(socket_message_WebsocketMessage& msg, const socket_message_AnalyticsData& data) {
        msg.message.analytics = data;
    }
};

template <>
struct MessageTraits<socket_message_AnglesData> {
    static constexpr pb_size_t tag = socket_message_WebsocketMessage_angles_tag;
    static void assign(socket_message_WebsocketMessage& msg, const socket_message_AnglesData& data) {
        msg.message.angles = data;
    }
};

template <>
struct MessageTraits<socket_message_RSSIData> {
    static constexpr pb_size_t tag = socket_message_WebsocketMessage_rssi_tag;
    static void assign(socket_message_WebsocketMessage& msg, const socket_message_RSSIData& data) {
        msg.message.rssi = data;
    }
};

template <>
struct MessageTraits<socket_message_KinematicData> {
    static constexpr pb_size_t tag = socket_message_WebsocketMessage_kinematic_data_tag;
    static void assign(socket_message_WebsocketMessage& msg, const socket_message_KinematicData& data) {
        msg.message.kinematic_data = data;
    }
};

template <>
struct MessageTraits<socket_message_IMUCalibrateData> {
    static constexpr pb_size_t tag = socket_message_WebsocketMessage_imu_calibrate_tag;
    static void assign(socket_message_WebsocketMessage& msg, const socket_message_IMUCalibrateData& data) {
        msg.message.imu_calibrate = data;
    }
};

template <>
struct MessageTraits<socket_message_I2CScanData> {
    static constexpr pb_size_t tag = socket_message_WebsocketMessage_i2c_scan_tag;
    static void assign(socket_message_WebsocketMessage& msg, const socket_message_I2CScanData& data) {
        msg.message.i2c_scan = data;
    }
};

template <>
struct MessageTraits<socket_message_PeripheralSettingsData> {
    static constexpr pb_size_t tag = socket_message_WebsocketMessage_peripheral_settings_tag;
    static void assign(socket_message_WebsocketMessage& msg, const socket_message_PeripheralSettingsData& data) {
        msg.message.peripheral_settings = data;
    }
};

class CommAdapterBase {
  public:
    CommAdapterBase() { mutex_ = xSemaphoreCreateMutex(); }
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
    void emit(const T& data, int clientId = -1) {
        constexpr pb_size_t tag = MessageTraits<T>::tag;

        if (clientId < 0 && !hasSubscribers(tag)) return;

        msg_.which_message = tag;
        MessageTraits<T>::assign(msg_, data);

        pb_ostream_t stream = pb_ostream_from_buffer(buffer_, sizeof(buffer_));
        if (!pb_encode(&stream, socket_message_WebsocketMessage_fields, &msg_)) {
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
        msg_.which_message = socket_message_WebsocketMessage_pongmsg_tag;
        msg_.message.pongmsg = socket_message_PongMsg_init_zero;
        pb_ostream_t stream = pb_ostream_from_buffer(pongBuffer, sizeof(pongBuffer));
        if (pb_encode(&stream, socket_message_WebsocketMessage_fields, &msg_)) {
            send(pongBuffer, stream.bytes_written, cid);
        }
    }

    void setupDecoderHandlers() {
        decoder_.onSubscribe([this](int32_t tag, int cid) { subscribe(tag, cid); });

        decoder_.onUnsubscribe([this](int32_t tag, int cid) { unsubscribe(tag, cid); });

        decoder_.onPing([this](int cid) { sendPong(cid); });
    }

    SemaphoreHandle_t mutex_;
    std::map<int32_t, std::list<int>> client_subscriptions_;
    ProtoDecoder decoder_;
    socket_message_WebsocketMessage msg_ = socket_message_WebsocketMessage_init_zero;
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
