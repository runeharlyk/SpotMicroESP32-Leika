#pragma once

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <esp_log.h>
#include <utils/ip_address.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <wifi/dns_reply.h>
#include <atomic>

#define DNS_PORT 53

/**
 * The access point's captive portal: answers every address query with the robot. The task that serves
 * it owns the socket; stop() waits for that task to leave, since it reads this object until it does.
 */
class DNSServer {
  public:
    DNSServer() : _exited(xSemaphoreCreateBinary()) {}
    ~DNSServer() {
        stop();
        vSemaphoreDelete(_exited);
    }

    bool start(uint16_t port, const IPAddress& resolvedIP) {
        if (_task) return true;

        uint32_t ip = static_cast<uint32_t>(resolvedIP);
        for (int i = 0; i < 4; i++) _address[i] = (ip >> (8 * i)) & 0xFF;

        _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (_socket < 0) {
            ESP_LOGE("DNSServer", "Failed to create socket");
            return false;
        }

        int opt = 1;
        setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // How long stop() can wait: the task sees it between receives.
        struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
        setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in serverAddr = {};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(port);

        if (bind(_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            ESP_LOGE("DNSServer", "Failed to bind socket");
            close(_socket);
            _socket = -1;
            return false;
        }

        _running = true;
        if (xTaskCreate(dnsTask, "dns_server", 4096, this, 3, &_task) != pdPASS) {
            ESP_LOGE("DNSServer", "Failed to start its task");
            _running = false;
            _task = nullptr;
            close(_socket);
            _socket = -1;
            return false;
        }

        ESP_LOGI("DNSServer", "Started on port %d, resolving to %s", port, resolvedIP.toString().c_str());
        return true;
    }

    void stop() {
        if (!_task) return;
        _running = false;
        xSemaphoreTake(_exited, portMAX_DELAY);
        _task = nullptr;
        close(_socket);
        _socket = -1;
        ESP_LOGI("DNSServer", "Stopped");
    }

  private:
    static void dnsTask(void* param) {
        DNSServer* self = static_cast<DNSServer*>(param);
        self->run();
        xSemaphoreGive(self->_exited);
        vTaskDelete(nullptr);
    }

    void run() {
        uint8_t query[DNS_MAX_PACKET_SIZE];
        uint8_t reply[DNS_MAX_PACKET_SIZE];
        struct sockaddr_in clientAddr;

        while (_running) {
            socklen_t clientAddrLen = sizeof(clientAddr);
            int len = recvfrom(_socket, query, sizeof(query), 0, (struct sockaddr*)&clientAddr, &clientAddrLen);
            if (len <= 0) continue;
            size_t replyLen = dnsReply(query, len, _address, reply, sizeof(reply));
            if (replyLen) sendto(_socket, reply, replyLen, 0, (struct sockaddr*)&clientAddr, clientAddrLen);
        }
    }

    int _socket = -1;
    uint8_t _address[4] = {};
    std::atomic<bool> _running {false};
    TaskHandle_t _task = nullptr;
    SemaphoreHandle_t _exited;
};
