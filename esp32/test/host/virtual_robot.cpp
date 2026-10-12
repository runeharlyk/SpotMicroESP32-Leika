// The robot's control and socket code on the host, for the app and the Python client to drive without hardware:
// MotionService, the ServoController on the fake I2C bus, CommAdapterBase's subscriptions and the firmware's telemetry,
// stepped every 10 ms as the control task steps them. simulation/virtual_robot.py builds it and serves it on a
// WebSocket. Usage: virtual_robot <variant> <elf sha256>
//
// It talks to the wrapper over stdin and stdout, each frame <kind: u8><client: u32 LE><length: u32 LE><bytes>, kind 'm'
// for a message and 'c' (no bytes) for a client that left: pipes need no ports or libraries on either side, and the
// program ends with its wrapper, when stdin closes.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <communication/comm_base.hpp>
#include <motion.h>
#include <peripherals/servo_controller.h>
#include <telemetry/tick_sample.h>
#include <variant.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace FileSystem {
bool mkdirRecursive(const char *) { return true; }
bool replaceFile(const char *, const char *) { return false; }
} // namespace FileSystem

namespace {

struct Frame {
    uint8_t kind;
    uint32_t client;
    std::vector<uint8_t> bytes;
};

constexpr size_t HEADER_SIZE = 9;

std::mutex framesMutex;
std::deque<Frame> frames;
std::atomic<bool> wrapperGone {false};

uint32_t readU32(const uint8_t *bytes) {
    return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | static_cast<uint32_t>(bytes[3]) << 24;
}

void writeU32(uint8_t *bytes, uint32_t value) {
    for (int i = 0; i < 4; i++) bytes[i] = value >> (8 * i);
}

// Blocks on stdin on its own thread, so the control loop keeps its period; only the loop touches the firmware code.
void readFrames() {
    uint8_t header[HEADER_SIZE];
    while (fread(header, 1, HEADER_SIZE, stdin) == HEADER_SIZE) {
        Frame frame {header[0], readU32(header + 1), std::vector<uint8_t>(readU32(header + 5))};
        if (fread(frame.bytes.data(), 1, frame.bytes.size(), stdin) != frame.bytes.size()) break;
        std::lock_guard<std::mutex> lock(framesMutex);
        frames.push_back(std::move(frame));
    }
    wrapperGone = true;
}

std::deque<Frame> takeFrames() {
    std::lock_guard<std::mutex> lock(framesMutex);
    return std::exchange(frames, {});
}

class PipeLink : public CommAdapterBase {
  public:
    void receive(const Frame &frame) {
        if (frame.kind == 'c') removeClient(frame.client);
        else handleIncoming(frame.bytes.data(), frame.bytes.size(), frame.client);
    }

  protected:
    bool send(const uint8_t *data, size_t len, int cid) override {
        uint8_t header[HEADER_SIZE] = {'m'};
        writeU32(header + 1, cid);
        writeU32(header + 5, len);
        return fwrite(header, 1, HEADER_SIZE, stdout) == HEADER_SIZE && fwrite(data, 1, len, stdout) == len;
    }
};

PipeLink pipeLink;
MotionService motionService;
ServoController servoController;
Telemetry telemetry;
KinematicsVariant variant;
char elfSha256[65];

const auto boot = std::chrono::steady_clock::now();

// The firmware code reads the stubbed esp_timer; it shows the time since start whenever it is read here.
int64_t clockUs() {
    using namespace std::chrono;
    return fake_timer::nowUs = duration_cast<microseconds>(steady_clock::now() - boot).count() + 1;
}

// Level and still, as an IMU on a flat floor reads.
ImuSample levelImu(int64_t now) {
    ImuSample imu;
    imu.t_us = now;
    imu.accel = {0, 0, 9.81f};
    imu.valid = 1 | 2 | 8;
    return imu;
}

char *text(const char *value) { return const_cast<char *>(value); }

void reportFeatures(socket_message_CorrelationResponse &res) {
    res.which_response = socket_message_CorrelationResponse_features_data_response_tag;
    auto &features = res.response.features_data_response;
    features.variant = text(variantName(variant));
    features.firmware_built_target = text("virtual");
    features.firmware_name = text(APP_NAME);
    features.firmware_version = text(APP_VERSION);
    std::strncpy(features.firmware_elf_sha256, elfSha256, sizeof(features.firmware_elf_sha256) - 1);
    features.imu = features.imu_detected = true;
    features.servo = features.servo_detected = servoController.detected();
    features.device_id = text("virtual");
    std::strncpy(features.robot_name, "Virtual robot", sizeof(features.robot_name) - 1);
    std::strncpy(features.hostname, "localhost", sizeof(features.hostname) - 1);
}

void answer(const socket_message_CorrelationRequest &req, int clientId) {
    socket_message_CorrelationResponse res = socket_message_CorrelationResponse_init_default;
    res.correlation_id = req.correlation_id;
    res.status_code = 200;
    switch (req.which_request) {
        case socket_message_CorrelationRequest_features_data_request_tag: reportFeatures(res); break;
        case socket_message_CorrelationRequest_servo_settings_request_tag:
            res.which_response = socket_message_CorrelationResponse_servo_settings_tag;
            servoController.protoHandler.read(res.response.servo_settings);
            break;
        default:
            res.status_code = 400;
            std::strncpy(res.error_message, "Not available on the virtual robot", sizeof(res.error_message) - 1);
    }
    pipeLink.emit(res, clientId);
}

void sendTelemetryHeader(int clientId) {
    socket_message_TelemetryHeader header = socket_message_TelemetryHeader_init_zero;
    header.firmware_version = text(APP_VERSION);
    header.build_target = text("virtual");
    header.variant = text(variantName(variant));
    header.device_id = text("virtual");
    header.imu_driver = text("virtual");
    header.imu_rate_hz = 100;
    header.control_rate_hz = 100;
    header.has_servo_settings = true;
    servoController.protoHandler.read(header.servo_settings);
    header.batch_ticks = Telemetry::BATCH_TICKS;
    std::copy(std::begin(JOINT_DIRECTION), std::end(JOINT_DIRECTION),
              header.joint_direction);
    pipeLink.emit(header, clientId);
}

// The firmware's socket handlers for driving the robot (main.cpp's setupEventSocket).
void handleMessages() {
    pipeLink.on<socket_message_ControllerData>([](const socket_message_ControllerData &data, int) {
        const int64_t now = esp_timer_get_time();
        motionService.inbox.postInput(data, now / 1000, now);
    });
    pipeLink.on<socket_message_ModeData>(
        [](const socket_message_ModeData &data, int) { motionService.inbox.postMode(data.mode); });
    pipeLink.on<socket_message_WalkGaitData>(
        [](const socket_message_WalkGaitData &data, int) { motionService.inbox.postGait(data.gait); });
    pipeLink.on<socket_message_CorrelationRequest>(answer);
    pipeLink.onSubscribed([](int32_t tag, int clientId) {
        if (tag == socket_message_Message_telemetry_batch_tag) sendTelemetryHeader(clientId);
    });
}

// As main.cpp's publishMotion: the robot reports its mode and gait on a change and every second.
void publishMotion() {
    static socket_message_ModesEnum lastMode = socket_message_ModesEnum_DEACTIVATED;
    static socket_message_WalkGaits lastGait = socket_message_WalkGaits_TROT;
    static int64_t lastSentAt = 0;
    const int64_t now = esp_timer_get_time() / 1000;
    const socket_message_ModesEnum mode = motionService.mode();
    const socket_message_WalkGaits gait = motionService.gait();
    if (mode == lastMode && gait == lastGait && now - lastSentAt < 1000) return;
    lastMode = mode;
    lastGait = gait;
    lastSentAt = now;
    pipeLink.emit(socket_message_ModeData {.mode = mode});
    pipeLink.emit(socket_message_WalkGaitData {.gait = gait});
}

void streamTelemetry() {
    const bool listening = pipeLink.hasSubscribers(socket_message_Message_telemetry_batch_tag);
    telemetry.setRecording(listening);
    if (!listening) telemetry.discardUnsent();
    static socket_message_TelemetryBatch batch;
    while (telemetry.takeBatch(batch)) pipeLink.emit(batch);
}

// The control task's tick (main.cpp's SpotControlLoopEntry), with the level IMU and no gestures.
void controlTick(uint32_t seq, int64_t previousStart, int64_t start) {
    const ImuSample imu = levelImu(start);
    motionService.update(imu, eGestureNone);
    if (motionService.takeModeApplied()) {
        servoController.setMode(SERVO_CONTROL_STATE::ANGLE);
        motionService.isActive() ? servoController.activate() : servoController.deactivate();
    }
    servoController.setAngles(motionService.getAngles());
    const int64_t computed = clockUs();
    const ServoWrite write = servoController.update((start - previousStart) / 1e6f);
    const int64_t written = clockUs();
    if (telemetry.recording()) {
        static socket_message_TickSample tick;
        fillTickSample(tick, {seq, start, previousStart, computed, written}, imu, write, motionService, servoController);
        telemetry.record(tick);
    }
}

// Windows sleeps in 15.6 ms steps but for a high-resolution timer.
void sleepUntil(std::chrono::steady_clock::time_point wake) {
#ifdef _WIN32
    static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due;
    due.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(wake - std::chrono::steady_clock::now()).count() / 100;
    if (due.QuadPart < 0 && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
#else
    std::this_thread::sleep_until(wake);
#endif
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: virtual_robot <variant> <elf sha256>\n");
        return 2;
    }
    for (int v = 0; v <= socket_message_KinematicsVariant_SPOTMICRO_YERTLE; v++)
        if (std::strcmp(argv[1], variantName(static_cast<KinematicsVariant>(v))) == 0)
            variant = static_cast<KinematicsVariant>(v);
    if (!knownVariant(variant)) {
        std::fprintf(stderr, "unknown variant %s\n", argv[1]);
        return 2;
    }
    std::strncpy(elfSha256, argv[2], sizeof(elfSha256) - 1);
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    clockUs();
    I2CBus::instance().begin(21, 22);
    motionService.useConfig(kinConfigFor(variant));
    servoController.useJointModel(jointModelFor(variant));
    servoController.begin();
    motionService.begin();
    handleMessages();
    std::thread reader(readFrames);

    using namespace std::chrono;
    constexpr auto PERIOD = milliseconds(10);
    auto wake = steady_clock::now();
    uint32_t seq = 0;
    int64_t previousStart = 0;
    while (!wrapperGone) {
        const int64_t start = clockUs();
        for (const Frame &frame : takeFrames()) pipeLink.receive(frame);
        controlTick(seq++, previousStart, start);
        previousStart = start;
        publishMotion();
        streamTelemetry();
        fflush(stdout);
        {
            std::lock_guard<std::mutex> lock(fake_i2c::mutex);
            fake_i2c::written.clear();  // the fake keeps every transaction for the tests; nothing reads them here
        }
        // As the firmware's loop: an overrun starts the schedule afresh rather than catching up.
        wake += PERIOD;
        if (steady_clock::now() > wake) wake = steady_clock::now();
        else sleepUntil(wake);
    }
    reader.join();
    return 0;
}
