#pragma once

#include <cstdint>

struct PowerGuardConfig {
    float cutoffVoltage = 0; // 0: no cut-off
    float stallCurrent = 0;  // 0: no stall flag
    uint32_t stallMs = 0;
};

/**
 * Watches the servo rail's readings for a flat battery and a stall.
 *
 * Low voltage: the rail under the cut-off for CUTOFF_HOLD_MS. A rail that sags under a step and recovers is not cut
 * off, and once under the cut-off only a rise past it by CUTOFF_HYSTERESIS_V counts as recovered, so noise around the
 * threshold neither restarts the hold nor clears the flag. Servos going limp take their load off, so the rail recovers
 * by that much only when the battery still has charge.
 *
 * Stalled: the current over the stall current for the stall time, until it falls back to it.
 */
class PowerGuard {
  public:
    static constexpr uint32_t CUTOFF_HOLD_MS = 2000;
    static constexpr float CUTOFF_HYSTERESIS_V = 0.2f;

    void configure(const PowerGuardConfig &config) {
        _config = config;
        _under = _overCurrent = false;
    }

    void update(uint32_t nowMs, float voltage, float current) {
        if (_config.cutoffVoltage <= 0)
            _under = false;
        else if (!_under && voltage < _config.cutoffVoltage) {
            _under = true;
            _underSinceMs = nowMs;
        } else if (_under && voltage > _config.cutoffVoltage + CUTOFF_HYSTERESIS_V)
            _under = false;

        if (_config.stallCurrent <= 0 || current <= _config.stallCurrent)
            _overCurrent = false;
        else if (!_overCurrent) {
            _overCurrent = true;
            _overSinceMs = nowMs;
        }
        _nowMs = nowMs;
    }

    bool lowVoltage() const { return _under && _nowMs - _underSinceMs >= CUTOFF_HOLD_MS; }
    bool stalled() const { return _overCurrent && _nowMs - _overSinceMs >= _config.stallMs; }

  private:
    PowerGuardConfig _config;
    bool _under = false, _overCurrent = false;
    uint32_t _underSinceMs = 0, _overSinceMs = 0, _nowMs = 0;
};
