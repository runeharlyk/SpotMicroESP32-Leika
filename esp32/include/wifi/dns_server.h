#pragma once

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <esp_log.h>
#include <utils/ip_address.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>
#include <atomic>

#define DNS_PORT 53
#define DNS_MAX_PACKET_SIZE 512

class DNSServer {
  public:
    DNSServer() : _socket(-1), _running(false), _task(nullptr) {}
    ~DNSServer() { stop(); }

    bool start(uint16_t port, const char* domainName, const IPAddress& resolvedIP) {
        if (_running) return true;

        _port = port;
        _resolvedIP = resolvedIP;
        _domainName = domainName ? domainName : "*";

        _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (_socket < 0) {
            ESP_LOGE("DNSServer", "Failed to create socket");
            return false;
        }

        int opt = 1;
        setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in serverAddr = {};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(_port);

        if (bind(_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            ESP_LOGE("DNSServer", "Failed to bind socket");
            close(_socket);
            _socket = -1;
            return false;
        }

        _running = true;
        xTaskCreate(dnsTask, "dns_server", 4096, this, 3, &_task);

        ESP_LOGI("DNSServer", "Started on port %d, resolving to %s", _port, _resolvedIP.toString().c_str());
        return true;
    }

    void stop() {
        _running = false;
        if (_task) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            _task = nullptr;
        }
        if (_socket >= 0) {
            close(_socket);
            _socket = -1;
        }
        ESP_LOGI("DNSServer", "Stopped");
    }

    void processNextRequest() {}

  private:
    static void dnsTask(void* param) {
        DNSServer* self = static_cast<DNSServer*>(param);
        self->run();
        vTaskDelete(nullptr);
    }

    void run() {
        uint8_t buffer[DNS_MAX_PACKET_SIZE];
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);

        while (_running) {
            int len = recvfrom(_socket, buffer, DNS_MAX_PACKET_SIZE, 0, (struct sockaddr*)&clientAddr, &clientAddrLen);
            if (len > 0) {
                processRequest(buffer, len, &clientAddr);
            }
        }
    }

    void processRequest(uint8_t* buffer, int len, struct sockaddr_in* clientAddr) {
        if (len < 12) return;

        uint16_t flags = (buffer[2] << 8) | buffer[3];
        if ((flags & 0x8000) != 0) return;

        uint8_t response[DNS_MAX_PACKET_SIZE];
        memcpy(response, buffer, len);

        response[2] = 0x81;
        response[3] = 0x80;

        response[6] = 0x00;
        response[7] = 0x01;

        int responseLen = len;

        response[responseLen++] = 0xC0;
        response[responseLen++] = 0x0C;

        response[responseLen++] = 0x00;
        response[responseLen++] = 0x01;

        response[responseLen++] = 0x00;
        response[responseLen++] = 0x01;

        response[responseLen++] = 0x00;
        response[responseLen++] = 0x00;
        response[responseLen++] = 0x00;
        response[responseLen++] = 0x3C;

        response[responseLen++] = 0x00;
        response[responseLen++] = 0x04;

        uint32_t ip = static_cast<uint32_t>(_resolvedIP);
        response[responseLen++] = ip & 0xFF;
        response[responseLen++] = (ip >> 8) & 0xFF;
        response[responseLen++] = (ip >> 16) & 0xFF;
        response[responseLen++] = (ip >> 24) & 0xFF;

        sendto(_socket, response, responseLen, 0, (struct sockaddr*)clientAddr, sizeof(*clientAddr));
    }

    int _socket;
    uint16_t _port;
    IPAddress _resolvedIP;
    std::string _domainName;
    std::atomic<bool> _running;
    TaskHandle_t _task;
};
