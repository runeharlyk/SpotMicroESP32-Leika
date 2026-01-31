#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <esp_netif.h>

class IPAddress {
  public:
    IPAddress() : _addr {0, 0, 0, 0} {}

    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : _addr {a, b, c, d} {}

    IPAddress(uint32_t addr) {
        _addr[0] = addr & 0xFF;
        _addr[1] = (addr >> 8) & 0xFF;
        _addr[2] = (addr >> 16) & 0xFF;
        _addr[3] = (addr >> 24) & 0xFF;
    }

    IPAddress(const esp_ip4_addr_t& ip4) {
        _addr[0] = ip4.addr & 0xFF;
        _addr[1] = (ip4.addr >> 8) & 0xFF;
        _addr[2] = (ip4.addr >> 16) & 0xFF;
        _addr[3] = (ip4.addr >> 24) & 0xFF;
    }

    operator uint32_t() const {
        return static_cast<uint32_t>(_addr[0]) | (static_cast<uint32_t>(_addr[1]) << 8) |
               (static_cast<uint32_t>(_addr[2]) << 16) | (static_cast<uint32_t>(_addr[3]) << 24);
    }

    operator esp_ip4_addr_t() const {
        esp_ip4_addr_t ip4;
        ip4.addr = static_cast<uint32_t>(*this);
        return ip4;
    }

    bool operator==(const IPAddress& other) const {
        return _addr[0] == other._addr[0] && _addr[1] == other._addr[1] && _addr[2] == other._addr[2] &&
               _addr[3] == other._addr[3];
    }

    bool operator!=(const IPAddress& other) const { return !(*this == other); }

    uint8_t operator[](int index) const { return _addr[index]; }

    uint8_t& operator[](int index) { return _addr[index]; }

    bool fromString(const char* str) {
        int parts[4];
        if (sscanf(str, "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]) != 4) {
            return false;
        }
        for (int i = 0; i < 4; i++) {
            if (parts[i] < 0 || parts[i] > 255) return false;
            _addr[i] = static_cast<uint8_t>(parts[i]);
        }
        return true;
    }

    std::string toString() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d", _addr[0], _addr[1], _addr[2], _addr[3]);
        return std::string(buf);
    }

    const char* c_str() const {
        static char buf[16];
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d", _addr[0], _addr[1], _addr[2], _addr[3]);
        return buf;
    }

  private:
    uint8_t _addr[4];
};
