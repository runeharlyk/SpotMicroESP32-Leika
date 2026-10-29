// Host test of what the serial link takes on each port (console_uart.h through ProtoDecoder's filter) and of the
// frame size the USB port's streams need; built and run by test_host_programs.py.
#include <cstdio>
#include <limits>
#include <vector>
#include <communication/console_uart.h>
#include <communication/proto_helpers.h>
#include <communication/serial_frame.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static constexpr int UART = 0, USB = 1;

static std::vector<uint8_t> encoded(const socket_message_Message &message) {
    std::vector<uint8_t> out(serial_frame::MAX_MESSAGE * 2);
    pb_ostream_t stream = pb_ostream_from_buffer(out.data(), out.size());
    if (!pb_encode(&stream, socket_message_Message_fields, &message)) return {};
    out.resize(stream.bytes_written);
    return out;
}

static socket_message_Message subscription(pb_size_t tag) {
    socket_message_Message message = socket_message_Message_init_zero;
    message.which_message = socket_message_Message_sub_notif_tag;
    message.message.sub_notif.tag = tag;
    return message;
}

static socket_message_Message controllerInput() {
    socket_message_Message message = socket_message_Message_init_zero;
    message.which_message = socket_message_Message_controller_data_tag;
    message.message.controller_data.has_left = true;
    message.message.controller_data.left.y = 0.5f;
    return message;
}

static socket_message_Message request() {
    socket_message_Message message = socket_message_Message_init_zero;
    message.which_message = socket_message_Message_correlation_request_tag;
    message.message.correlation_request.correlation_id = 7;
    message.message.correlation_request.which_request = socket_message_CorrelationRequest_features_data_request_tag;
    return message;
}

// The serial link's decoder: the USB port takes everything, the console UART what console_uart::accepts.
struct SerialDecoder {
    ProtoDecoder decoder;
    std::vector<int> inputsFrom, requestsFrom;
    std::vector<std::pair<int32_t, int>> subscriptions;

    SerialDecoder() {
        decoder.filter([](const socket_message_Message &message, int cid) {
            return cid == USB || console_uart::accepts(message);
        });
        decoder.on<socket_message_ControllerData>(
            [this](const socket_message_ControllerData &, int cid) { inputsFrom.push_back(cid); });
        decoder.on<socket_message_CorrelationRequest>(
            [this](const socket_message_CorrelationRequest &, int cid) { requestsFrom.push_back(cid); });
        decoder.onSubscribe([this](int32_t tag, int cid) { subscriptions.push_back({tag, cid}); });
    }

    bool feed(const socket_message_Message &message, int cid) {
        const std::vector<uint8_t> bytes = encoded(message);
        return decoder.decode(bytes.data(), bytes.size(), cid);
    }
};

static void theUsbPortTakesDrivingAndStreams() {
    SerialDecoder link;
    CHECK(link.feed(controllerInput(), USB));
    CHECK(link.feed(subscription(socket_message_Message_telemetry_batch_tag), USB));
    CHECK(link.inputsFrom == std::vector<int>({USB}));
    CHECK(link.subscriptions.size() == 1 && link.subscriptions[0].first == socket_message_Message_telemetry_batch_tag);
}

// At 115200 baud beside the log, the console UART could carry neither a telemetry stream nor timely input.
static void theConsoleUartDropsDrivingAndStreamsButAnswersRequests() {
    SerialDecoder link;
    CHECK(link.feed(controllerInput(), UART));
    CHECK(link.feed(subscription(socket_message_Message_telemetry_batch_tag), UART));
    CHECK(link.feed(subscription(socket_message_Message_imu_tag), UART));
    CHECK(link.feed(request(), UART));
    CHECK(link.feed(subscription(socket_message_Message_wifi_status_tag), UART));
    CHECK(link.inputsFrom.empty());
    CHECK(link.requestsFrom == std::vector<int>({UART}));
    CHECK(link.subscriptions.size() == 1 && link.subscriptions[0].first == socket_message_Message_wifi_status_tag);
}

// The largest batch the robot can send, every field at the widest encoding it can reach: times since boot under 2^49 us
// (17 years), durations under 2^23 us (8 s; the task watchdog restarts a robot whose task is stuck 5 s), 16-bit PWM
// values, the tick's parts saturated at 16 bits and the command's age at 32. nanopb's socket_message_TelemetryBatch_size
// is over a frame's limit, but it counts the fixed arrays unpacked; they go out packed.
static void aFullTelemetryBatchFitsOneFrame() {
    static socket_message_Message message = socket_message_Message_init_zero;
    message.which_message = socket_message_Message_telemetry_batch_tag;
    socket_message_TelemetryBatch &batch = message.message.telemetry_batch;
    constexpr uint32_t u32 = std::numeric_limits<uint32_t>::max();
    constexpr uint32_t duration = (1u << 23) - 1;
    constexpr uint32_t part = std::numeric_limits<uint16_t>::max();
    constexpr uint64_t sinceBoot = (1ull << 49) - 1;
    batch.batch_seq = batch.dropped_ticks = u32;
    batch.ticks_count = sizeof(batch.ticks) / sizeof(batch.ticks[0]);
    for (pb_size_t i = 0; i < batch.ticks_count; i++) {
        socket_message_TickSample &tick = batch.ticks[i];
        tick.seq = tick.command_age_us = u32;
        tick.period_us = tick.compute_us = tick.servo_write_us = duration;
        tick.motion_us = tick.motion_step_us = tick.odometry_us = tick.goal_us = tick.kinematics_us = part;
        tick.servo_bus_wait_us = tick.servo_transfer_us = part;
        tick.t_us = tick.command_rx_us = sinceBoot;
        tick.servo_ok = tick.link_lost = true;
        tick.mode = _socket_message_ModesEnum_MAX;
        tick.gait = tick.walked = _socket_message_WalkGaits_MAX;
        tick.has_odometry = true;
        tick.odometry = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        for (float &angle : tick.angles) angle = 1.0f;
        for (float &target : tick.targets) target = 1.0f;
        for (uint32_t &pwm : tick.pwm) pwm = std::numeric_limits<uint16_t>::max();
        for (float &value : tick.command) value = 1.0f;
        tick.has_imu = true;
        tick.imu.t_us = tick.imu.mag_t_us = sinceBoot;
        tick.imu.temperature = 1.0f;
        tick.imu.valid = 31;
    }
    const std::vector<uint8_t> bytes = encoded(message);
    CHECK(!bytes.empty());
    CHECK(!serial_frame::encode(bytes.data(), bytes.size()).empty());
}

int main() {
    theUsbPortTakesDrivingAndStreams();
    theConsoleUartDropsDrivingAndStreamsButAnswersRequests();
    aFullTelemetryBatchFitsOneFrame();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
