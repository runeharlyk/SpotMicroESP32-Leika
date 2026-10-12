#pragma once

#include <cstdint>

/**
 * Tells a station that is joined but cannot reach its network: on the Pico the link stayed "connected" with no
 * disconnect event while nothing got through, and the firmware only reconnects on that event. The station probes its
 * gateway every PROBE_PERIOD_MS, and FAILURES_TO_RECONNECT unanswered probes in a row ask for a reconnect.
 * One probe at a time; a change of link (joined, dropped, reconnecting) starts the count afresh.
 */
class LinkWatchdog {
  public:
    static constexpr uint32_t PROBE_PERIOD_MS = 5000;
    static constexpr int FAILURES_TO_RECONNECT = 3;

    bool probeDue(uint32_t nowMs) const { return !_probing && nowMs - _lastProbeMs >= PROBE_PERIOD_MS; }

    void probeStarted(uint32_t nowMs) {
        _probing = true;
        _lastProbeMs = nowMs;
    }

    /** Whether the link has failed often enough in a row to reconnect; a reconnect starts the count afresh. */
    bool probeEnded(bool answered) {
        _probing = false;
        _failures = answered ? 0 : _failures + 1;
        if (_failures < FAILURES_TO_RECONNECT) return false;
        _failures = 0;
        return true;
    }

    void linkChanged(uint32_t nowMs) {
        _failures = 0;
        _probing = false;
        _lastProbeMs = nowMs;
    }

  private:
    bool _probing = false;
    int _failures = 0;
    uint32_t _lastProbeMs = 0;
};
