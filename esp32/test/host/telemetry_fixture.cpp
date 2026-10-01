// Writes the telemetry fixture the simulation's loader is tested against, with the firmware's own types and
// encoder. Built and run by export_telemetry_fixture.py.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <pb_encode.h>
#include <telemetry/telemetry.h>

static void writeRecord(FILE *file, uint64_t hostRxUs, const socket_message_Message &message) {
    std::vector<uint8_t> buffer(8192);
    pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), buffer.size());
    if (!pb_encode(&stream, socket_message_Message_fields, &message)) {
        std::fprintf(stderr, "encode failed: %s\n", PB_GET_ERROR(&stream));
        std::exit(1);
    }
    const uint32_t length = static_cast<uint32_t>(stream.bytes_written);
    std::fwrite(&hostRxUs, sizeof(hostRxUs), 1, file);  // the host is little-endian, as the format is
    std::fwrite(&length, sizeof(length), 1, file);
    std::fwrite(buffer.data(), 1, length, file);
}

static void writeGap(FILE *file, uint64_t hostRxUs) {
    const uint32_t length = 0;
    std::fwrite(&hostRxUs, sizeof(hostRxUs), 1, file);
    std::fwrite(&length, sizeof(length), 1, file);
}

static socket_message_TickSample tick(uint32_t n) {
    socket_message_TickSample t = socket_message_TickSample_init_zero;
    t.seq = n;
    t.t_us = 1000000 + 10000ULL * n;
    t.period_us = n ? 10000 : 0;
    t.compute_us = 1500;
    t.servo_write_us = 2000;
    t.servo_ok = true;
    t.has_imu = true;
    ImuSample imu;
    imu.t_us = static_cast<int64_t>(t.t_us) - 3000;
    imu.accel = {0, 0, 9.81f};
    imu.gyro = {0.01f, -0.02f, 0.005f};
    imu.valid = 15;
    imuToProto(imu, t.imu);
    if (n >= 5) {
        t.command_rx_us = t.t_us - 20000;
        t.command_age_us = 20000;
    }
    for (int i = 0; i < 12; i++) t.angles[i] = t.targets[i] = 0.1f * i;
    t.mode = socket_message_ModesEnum_WALK;
    return t;
}

int main(int argc, char **argv) {
    FILE *file = std::fopen(argv[1], "wb");
    const uint16_t version = 1;
    std::fwrite("LEIKAREC", 1, 8, file);
    std::fwrite(&version, sizeof(version), 1, file);

    static socket_message_Message message;
    message = socket_message_Message_init_zero;
    message.which_message = socket_message_Message_telemetry_header_tag;
    message.message.telemetry_header.firmware_version = const_cast<char *>("fixture");
    message.message.telemetry_header.variant = const_cast<char *>("SPOTMICRO_ESP32_MINI");
    message.message.telemetry_header.imu_driver = const_cast<char *>("MPU6050");
    message.message.telemetry_header.imu_rate_hz = 200;
    message.message.telemetry_header.control_rate_hz = 100;
    message.message.telemetry_header.batch_ticks = 10;
    writeRecord(file, 0, message);

    static Telemetry telemetry;
    telemetry.setRecording(true);
    static socket_message_TelemetryBatch batch;
    for (uint32_t b = 0; b < 4; b++) {
        for (uint32_t n = b * 10; n < b * 10 + 10; n++) telemetry.record(tick(n));
        telemetry.takeBatch(batch);
        if (b == 2) continue;  // lost on the way
        message = socket_message_Message_init_zero;
        message.which_message = socket_message_Message_telemetry_batch_tag;
        message.message.telemetry_batch = batch;
        writeRecord(file, 1100000 + 100000ULL * b, message);
    }
    writeGap(file, 1500000);
    message = socket_message_Message_init_zero;
    message.which_message = socket_message_Message_telemetry_network_tag;
    message.message.telemetry_network.t_us = 1400000;
    message.message.telemetry_network.rssi = -57;
    message.message.telemetry_network.batches_sent = 4;
    writeRecord(file, 1600000, message);
    std::fclose(file);
    return 0;
}
