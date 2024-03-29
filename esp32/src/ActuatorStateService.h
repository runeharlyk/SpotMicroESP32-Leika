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

class ActuatorState
{
public:
    int16_t state[12] = {0, 45, -90, 0, 45, -90, 0, 45, -90, 0, 45, -90};

    static void read(ActuatorState &settings, JsonObject &root)
    {
        JsonArray array = root.createNestedArray("state");
        for(int i = 0; i < 12; i++)
        {
            array.add(settings.state[i]);
        }
    }

    static StateUpdateResult update(JsonObject &root, ActuatorState &actuatorState)
    {
        Serial.print("New state array: [");
        JsonArray array = root["state"];
        bool changed = false;
        for(int i = 0; i < 12; i++)
        {
            Serial.print(array[i].as<int16_t>());
            Serial.print(", ");
            if (actuatorState.state[i] != array[i].as<int16_t>())
            {
                actuatorState.state[i] = array[i];
                changed = true;
            }
        }
        Serial.println("]");
        return changed ? StateUpdateResult::CHANGED : StateUpdateResult::UNCHANGED;
    }

    static void homeAssistRead(ActuatorState &settings, JsonObject &root)
    {
        JsonArray array = root.createNestedArray("state");
        for(int i = 0; i < 12; i++)
        {
            array.add(settings.state[i]);
        }
    }

    static StateUpdateResult homeAssistUpdate(JsonObject &root, ActuatorState &actuatorState)
    {
        JsonArray array = root["state"];
        if(array.size() != 12) return StateUpdateResult::ERROR;
        
        bool changed = false;
        for(int i = 0; i < 12; i++)
        {
            if (actuatorState.state[i] != array[i].as<int16_t>())
            {
                actuatorState.state[i] = array[i];
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

    private:
        HttpEndpoint<ActuatorState> _httpEndpoint;
        WebSocketServer<ActuatorState> _webSocketServer;

        void onConfigUpdated();
};

#endif
