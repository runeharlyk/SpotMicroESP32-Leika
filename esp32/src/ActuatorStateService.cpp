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

#include <ActuatorStateService.h>

ActuatorStateService::ActuatorStateService(
    PsychicHttpServer *server,
    SecurityManager *securityManager
    ) : _httpEndpoint(
            ActuatorState::read,
            ActuatorState::update,
            this,
            server,
            ACTUATOR_SETTINGS_ENDPOINT_PATH,
            securityManager,
            AuthenticationPredicates::IS_AUTHENTICATED),
    _webSocketServer(
        ActuatorState::read,
        ActuatorState::update,
        this,
        server,
        ACTUATOR_SETTINGS_SOCKET_PATH,
        securityManager,
        AuthenticationPredicates::IS_AUTHENTICATED)
{
    // Setup actuator hardware

    addUpdateHandler([&](const String &originId){ onConfigUpdated(); }, false);
}

void ActuatorStateService::begin()
{
    _httpEndpoint.begin();
    _webSocketServer.begin();
    onConfigUpdated();
    xTaskCreatePinnedToCore(
        this->_loopImpl,            // Function that should be called
        "Motion Service",           // Name of the task (for debugging)
        4096,                       // Stack size (bytes)
        this,                       // Pass reference to this class instance
        (tskIDLE_PRIORITY + 1),     // task priority
        NULL,                       // Task handle
        ESP32SVELTEKIT_RUNNING_CORE // Pin to application core
    );
}

void ActuatorStateService::onConfigUpdated()
{
    log_i("Update hardware interface for actuators");
    // UPDATE HARDWARE INTERFACE FOR ACTUATORS
}
