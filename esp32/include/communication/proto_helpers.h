#pragma once

#include <pb_encode.h>
#include <pb_decode.h>
#include <platform_shared/message.pb.h>
#include <functional>
#include <map>

#define PROTO_BUFFER_SIZE 2048  // Increased for chunked file transfer responses

template <typename T>
struct MessageTraits;

#define DEFINE_MESSAGE_TRAITS(DataType, field)                                                   \
    template <>                                                                                  \
    struct MessageTraits<socket_message_##DataType> {                                            \
        static constexpr pb_size_t tag = socket_message_Message_##field##_tag;                   \
        static void assign(socket_message_Message& msg, const socket_message_##DataType& data) { \
            msg.message.field = data;                                                            \
        }                                                                                        \
        static const socket_message_##DataType& access(const socket_message_Message& msg) {      \
            return msg.message.field;                                                            \
        }                                                                                        \
    };

DEFINE_MESSAGE_TRAITS(IMUData, imu)
DEFINE_MESSAGE_TRAITS(ModeData, mode)
DEFINE_MESSAGE_TRAITS(AnalyticsData, analytics)
DEFINE_MESSAGE_TRAITS(AnglesData, angles)
DEFINE_MESSAGE_TRAITS(RSSIData, rssi)
DEFINE_MESSAGE_TRAITS(KinematicData, kinematic_data)
DEFINE_MESSAGE_TRAITS(IMUCalibrateData, imu_calibrate)
DEFINE_MESSAGE_TRAITS(I2CScanData, i2c_scan)
DEFINE_MESSAGE_TRAITS(PeripheralSettingsData, peripheral_settings)
DEFINE_MESSAGE_TRAITS(ControllerData, controller_data)
DEFINE_MESSAGE_TRAITS(WalkGaitData, walk_gait)
DEFINE_MESSAGE_TRAITS(IMUCalibrateExecute, imu_calibrate_execute)
DEFINE_MESSAGE_TRAITS(I2CScanDataRequest, i2c_scan_data_request)
DEFINE_MESSAGE_TRAITS(PeripheralSettingsDataRequest, peripheral_settings_data_request)
DEFINE_MESSAGE_TRAITS(ServoPWMData, servo_pwm)
DEFINE_MESSAGE_TRAITS(ServoStateData, servo_state)
DEFINE_MESSAGE_TRAITS(CorrelationRequest, correlation_request)
DEFINE_MESSAGE_TRAITS(CorrelationResponse, correlation_response)
// DEFINE_MESSAGE_TRAITS(FSDeleteRequest, fs_delete_request)
// DEFINE_MESSAGE_TRAITS(FSDeleteResponse, fs_delete_response)
// DEFINE_MESSAGE_TRAITS(FSMkdirRequest, fs_mkdir_request)
// DEFINE_MESSAGE_TRAITS(FSMkdirResponse, fs_mkdir_response)
// DEFINE_MESSAGE_TRAITS(FSListRequest, fs_list_request)
// DEFINE_MESSAGE_TRAITS(FSListResponse, fs_list_response)
// DEFINE_MESSAGE_TRAITS(FSDownloadStartRequest, fs_download_start_request)
// DEFINE_MESSAGE_TRAITS(FSDownloadStartResponse, fs_download_start_response)
// DEFINE_MESSAGE_TRAITS(FSDownloadChunkRequest, fs_download_chunk_request)
// DEFINE_MESSAGE_TRAITS(FSDownloadChunkResponse, fs_download_chunk_response)
// DEFINE_MESSAGE_TRAITS(FSUploadStartRequest, fs_upload_start_request)
// DEFINE_MESSAGE_TRAITS(FSUploadStartResponse, fs_upload_start_response)
// DEFINE_MESSAGE_TRAITS(FSUploadChunkRequest, fs_upload_chunk_request)
// DEFINE_MESSAGE_TRAITS(FSUploadChunkResponse, fs_upload_chunk_response)
// DEFINE_MESSAGE_TRAITS(FSCancelTransferRequest, fs_cancel_transfer_request)
// DEFINE_MESSAGE_TRAITS(FSCancelTransferResponse, fs_cancel_transfer_response)

#undef DEFINE_MESSAGE_TRAITS

class ProtoDecoder {
  public:
    using SubscribeHandler = std::function<void(int32_t tag, int clientId)>;
    using UnsubscribeHandler = std::function<void(int32_t tag, int clientId)>;
    using PingHandler = std::function<void(int clientId)>;

    void onSubscribe(SubscribeHandler handler) { subscribeHandler_ = handler; }
    void onUnsubscribe(UnsubscribeHandler handler) { unsubscribeHandler_ = handler; }
    void onPing(PingHandler handler) { pingHandler_ = handler; }

    template <typename T>
    void on(std::function<void(const T&, int)> handler) {
        handlers_[MessageTraits<T>::tag] = [handler, this](int clientId) {
            handler(MessageTraits<T>::access(msg_), clientId);
        };
    }

    bool decode(const uint8_t* data, size_t len, int clientId) {
        pb_istream_t stream = pb_istream_from_buffer(data, len);

        if (!pb_decode(&stream, socket_message_Message_fields, &msg_)) {
            return false;
        }

        switch (msg_.which_message) {
            case socket_message_Message_sub_notif_tag:
                if (subscribeHandler_) subscribeHandler_(msg_.message.sub_notif.tag, clientId);
                return true;

            case socket_message_Message_unsub_notif_tag:
                if (unsubscribeHandler_) unsubscribeHandler_(msg_.message.unsub_notif.tag, clientId);
                return true;

            case socket_message_Message_pingmsg_tag:
                if (pingHandler_) pingHandler_(clientId);
                return true;

            default: {
                auto it = handlers_.find(msg_.which_message);
                if (it != handlers_.end()) {
                    it->second(clientId);
                    return true;
                }
                return false;
            }
        }
    }

  private:
    socket_message_Message msg_ = socket_message_Message_init_zero;
    SubscribeHandler subscribeHandler_;
    UnsubscribeHandler unsubscribeHandler_;
    PingHandler pingHandler_;
    std::map<pb_size_t, std::function<void(int)>> handlers_;
};
