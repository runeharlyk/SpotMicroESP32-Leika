#pragma once

#include <pb_encode.h>
#include <pb_decode.h>
#include <platform_shared/websocket_message.pb.h>
#include <functional>

#define PROTO_BUFFER_SIZE 512

class ProtoDecoder {
  public:
    using SubscribeHandler = std::function<void(int32_t tag, int clientId)>;
    using UnsubscribeHandler = std::function<void(int32_t tag, int clientId)>;
    using PingHandler = std::function<void(int clientId)>;
    using ModeHandler = std::function<void(const socket_message_ModeData& data, int clientId)>;
    using InputHandler = std::function<void(const socket_message_HumanInputData& data, int clientId)>;
    using AnglesHandler = std::function<void(const socket_message_AnglesData& data, int clientId)>;
    using KinematicHandler = std::function<void(const socket_message_KinematicData& data, int clientId)>;
    using WalkGaitHandler = std::function<void(const socket_message_WalkGaitData& data, int clientId)>;
    using IMUCalibrateExecHandler = std::function<void(int clientId)>;
    using I2CScanRequestHandler = std::function<void(int clientId)>;
    using PeripheralSettingsRequestHandler = std::function<void(int clientId)>;

    void onSubscribe(SubscribeHandler handler) { subscribeHandler = handler; }
    void onUnsubscribe(UnsubscribeHandler handler) { unsubscribeHandler = handler; }
    void onPing(PingHandler handler) { pingHandler = handler; }
    void onMode(ModeHandler handler) { modeHandler = handler; }
    void onInput(InputHandler handler) { inputHandler = handler; }
    void onAngles(AnglesHandler handler) { anglesHandler = handler; }
    void onKinematic(KinematicHandler handler) { kinematicHandler = handler; }
    void onWalkGait(WalkGaitHandler handler) { walkGaitHandler = handler; }
    void onIMUCalibrateExec(IMUCalibrateExecHandler handler) { imuCalibrateExecHandler = handler; }
    void onI2CScanRequest(I2CScanRequestHandler handler) { i2cScanRequestHandler = handler; }
    void onPeripheralSettingsRequest(PeripheralSettingsRequestHandler handler) {
        peripheralSettingsRequestHandler = handler;
    }

    bool decode(const uint8_t* data, size_t len, int clientId) {
        pb_istream_t stream = pb_istream_from_buffer(data, len);

        if (!pb_decode(&stream, socket_message_WebsocketMessage_fields, &msg_)) {
            return false;
        }

        switch (msg_.which_message) {
            case socket_message_WebsocketMessage_sub_notif_tag:
                if (subscribeHandler) subscribeHandler(msg_.message.sub_notif.tag, clientId);
                break;

            case socket_message_WebsocketMessage_unsub_notif_tag:
                if (unsubscribeHandler) unsubscribeHandler(msg_.message.unsub_notif.tag, clientId);
                break;

            case socket_message_WebsocketMessage_pingmsg_tag:
                if (pingHandler) pingHandler(clientId);
                break;

            case socket_message_WebsocketMessage_mode_tag:
                if (modeHandler) modeHandler(msg_.message.mode, clientId);
                break;

            case socket_message_WebsocketMessage_human_input_data_tag:
                if (inputHandler) inputHandler(msg_.message.human_input_data, clientId);
                break;

            case socket_message_WebsocketMessage_angles_tag:
                if (anglesHandler) anglesHandler(msg_.message.angles, clientId);
                break;

            case socket_message_WebsocketMessage_kinematic_data_tag:
                if (kinematicHandler) kinematicHandler(msg_.message.kinematic_data, clientId);
                break;

            case socket_message_WebsocketMessage_walk_gait_tag:
                if (walkGaitHandler) walkGaitHandler(msg_.message.walk_gait, clientId);
                break;

            case socket_message_WebsocketMessage_imu_calibrate_execute_tag:
                if (imuCalibrateExecHandler) imuCalibrateExecHandler(clientId);
                break;

            case socket_message_WebsocketMessage_i2c_scan_data_request_tag:
                if (i2cScanRequestHandler) i2cScanRequestHandler(clientId);
                break;

            case socket_message_WebsocketMessage_peripheral_settings_data_request_tag:
                if (peripheralSettingsRequestHandler) peripheralSettingsRequestHandler(clientId);
                break;

            default: return false;
        }

        return true;
    }

  private:
    socket_message_WebsocketMessage msg_ = socket_message_WebsocketMessage_init_zero;
    SubscribeHandler subscribeHandler;
    UnsubscribeHandler unsubscribeHandler;
    PingHandler pingHandler;
    ModeHandler modeHandler;
    InputHandler inputHandler;
    AnglesHandler anglesHandler;
    KinematicHandler kinematicHandler;
    WalkGaitHandler walkGaitHandler;
    IMUCalibrateExecHandler imuCalibrateExecHandler;
    I2CScanRequestHandler i2cScanRequestHandler;
    PeripheralSettingsRequestHandler peripheralSettingsRequestHandler;
};
