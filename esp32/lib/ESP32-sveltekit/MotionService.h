#ifndef MotionService_h
#define MotionService_h

#include <EventSocket.h>
#include <TaskManager.h>

#define DEFAULT_STATE false
#define LIGHT_SETTINGS_ENDPOINT_PATH "/api/input"
#define ANGLES_EVENT "angles"
#define INPUT_EVENT "input"
#define MODE_EVENT "mode"

#define MOTION_INTERVAL 100

enum class MOTION_STATE
{
    IDLE,
    REST,
    STAND,
    WALK
};

class MotionService
{
  public:
    MotionService(PsychicHttpServer *server, EventSocket *socket, SecurityManager *securityManager, TaskManager *taskManager)
        : _server(server), _socket(socket), _securityManager(securityManager), _taskManager(taskManager)
    {
    }

    void begin()
    {
        _socket->registerEvent(INPUT_EVENT);
        _socket->registerEvent(ANGLES_EVENT);
        _socket->registerEvent(MODE_EVENT);

        _socket->onEvent(INPUT_EVENT, [&](JsonObject &root, int originId) { handleInput(root, originId); });

        _socket->onEvent(MODE_EVENT, [&](JsonObject &root, int originId) { handleMode(root, originId); });

        _socket->onEvent(ANGLES_EVENT, [&](JsonObject &root, int originId) { anglesEvent(root, originId); });
        _socket->onSubscribe(ANGLES_EVENT,
                             std::bind(&MotionService::syncState, this, std::placeholders::_1, std::placeholders::_2));
    }

    void syncState(const String &originId, bool sync = false)
    {
        DynamicJsonDocument jsonDocument{200};
        char output[200];
        JsonObject root = jsonDocument.to<JsonObject>();
        root["angles"] = angles;
        serializeJson(root, output);
        ESP_LOGV("MotionState", "Syncing state: %s", output);
        _socket->emit(ANGLES_EVENT, output, originId.c_str());
    }

    void anglesEvent(JsonObject &root, int originId)
    {
        JsonArray array = root["data"].as<JsonArray>();
        for (int i = 0; i < 12; i++)
        {
            angles[i] = array[i];
        }
        char output[100];
        serializeJson(array, output);
        sprintf(output, "[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]", angles[0], angles[1], angles[2], angles[3], angles[4],
                angles[5], angles[6], angles[7], angles[8], angles[9], angles[10], angles[11]);
        _socket->emit(ANGLES_EVENT, output, String(originId).c_str());
    }

    void handleInput(JsonObject &root, int originId)
    {
        String jsonString;
        JsonArray array = root["data"].as<JsonArray>();
        serializeJson(array, jsonString);
        ESP_LOGI("MotionService", "%s", jsonString.c_str());
        for (int i = 0; i < 7; i++)
        {
            input[i] = array[i];
        }
        ESP_LOGI("MotionService", "Input: %d %d %d %d %d %d %d", input[0], input[1], input[2], input[3], input[4], input[5], input[6]);
    }

    void handleMode(JsonObject &root, int originId)
    {
        ESP_LOGV("MotionService", "Mode %d", root["data"].as<int>());
        motionState = (MOTION_STATE)root["data"].as<int>();
        char output[2];
        sprintf(output, "%d", motionState);
        _socket->emit(MODE_EVENT, output, String(originId).c_str());
    }

  private:
    PsychicHttpServer *_server;
    EventSocket *_socket;
    SecurityManager *_securityManager;
    TaskManager *_taskManager;
    int8_t input[7] = {0, 0, 0, 0, 0, 0, 0};
    int16_t angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    MOTION_STATE motionState = MOTION_STATE::IDLE;
};

#endif
