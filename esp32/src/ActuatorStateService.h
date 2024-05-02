#ifndef LightStateService_h
#define LightStateService_h

/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2018 - 2023 rjwats
 *   Copyright (C) 2023 theelims
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#include <HttpEndpoint.h>
#include <WebSocketServer.h>

#define ACTUATOR_SETTINGS_ENDPOINT_PATH "/rest/actuators"
#define ACTUATOR_SETTINGS_SOCKET_PATH "/ws"

#define MOTION_INTERVAL 100

#define MAX_ESP_ANGLE_SIZE 256

enum class MOTION_STATE
{
    IDLE,
    REST,
    STAND,
    WALK
};

class ActuatorState
{
public:
    int16_t angles[12] = {0, 45, -90, 0, 45, -90, 0, 45, -90, 0, 45, -90};
    int8_t controller[7] = {0, 0, 0, 0, 0, 0 ,0};
    MOTION_STATE motionState = MOTION_STATE::IDLE;

    static void read(ActuatorState &settings, JsonObject &root)
    {
        root["type"] = "angles";
        JsonArray array = root.createNestedArray("data");
        for(int i = 0; i < 12; i++)
        {
            array.add(settings.angles[i]);
        }
    }

    static StateUpdateResult update(JsonObject &root, ActuatorState &actuatorState)
    {
        if (root["type"] == "mode") {
            if (actuatorState.motionState == (MOTION_STATE)root["data"].as<int>()) return StateUpdateResult::UNCHANGED;
            actuatorState.motionState = (MOTION_STATE)root["data"].as<int>();
            return StateUpdateResult::UNCHANGED;
        }
        if (root["type"] != "angles") return StateUpdateResult::UNCHANGED;
        JsonArray array = root["data"];
        bool changed = false;
        for(int i = 0; i < 12; i++)
        {
            if (actuatorState.angles[i] != array[i].as<int16_t>())
            {
                actuatorState.angles[i] = array[i];
                //changed = true;
            }
        }
        return changed ? StateUpdateResult::CHANGED : StateUpdateResult::UNCHANGED;
    }

    static void homeAssistRead(ActuatorState &settings, JsonObject &root)
    {
        JsonArray array = root.createNestedArray("angles");
        for(int i = 0; i < 12; i++)
        {
            array.add(settings.angles[i]);
        }
    }

    static StateUpdateResult homeAssistUpdate(JsonObject &root, ActuatorState &actuatorState)
    {
        JsonArray array = root["angles"];
        if(array.size() != 12) return StateUpdateResult::ERROR;
        
        bool changed = false;
        for(int i = 0; i < 12; i++)
        {
            if (actuatorState.angles[i] != array[i].as<int16_t>())
            {
                actuatorState.angles[i] = array[i];
                changed = true;
            }
        }
        return changed ? StateUpdateResult::CHANGED : StateUpdateResult::UNCHANGED;
    }
};

class ActuatorStateService : public StatefulService<ActuatorState> {
    public:
        ActuatorStateService(PsychicHttpServer *server, SecurityManager *securityManager);
        void begin();
    protected:
        static void _loopImpl(void *_this) { static_cast<ActuatorStateService *>(_this)->_loop(); }
        void _loop()
        {
            TickType_t xLastWakeTime;
            xLastWakeTime = xTaskGetTickCount();
            int dir = 2;
            while (1)
            {
                _state.angles[1] += dir;
                if (_state.angles[1] >= 90) dir = -1;
                if (_state.angles[1] <= 0) dir = 1;
                StaticJsonDocument<MAX_ESP_ANGLE_SIZE> doc;
                String message;
                JsonArray array = doc.createNestedArray("angles");
                for(int16_t num : _state.angles) {
                    array.add(num);
                }
                doc["mode"] = (int)_state.motionState;

                serializeJson(doc, message);
                // _notificationEvents->send(message, "motion", millis());
                vTaskDelayUntil(&xLastWakeTime, MOTION_INTERVAL / portTICK_PERIOD_MS);
            }
        };
    private:
        HttpEndpoint<ActuatorState> _httpEndpoint;
        WebSocketServer<ActuatorState> _webSocketServer;

        void onConfigUpdated();
};

#endif
