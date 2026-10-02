#pragma once
// Host stand-in for the ROM busy-wait: counts the microseconds spun instead of spinning.
#include <cstdint>
namespace fake_rom {
inline uint64_t spunUs = 0;
}
inline void esp_rom_delay_us(uint32_t us) { fake_rom::spunUs += us; }
