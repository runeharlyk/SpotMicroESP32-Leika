#pragma once

#include <algorithm>
#include <atomic>
#include <platform_shared/message.pb.h>
#include <peripherals/imu/imu_math.h>
#include <telemetry/spsc_ring.h>

inline void imuToProto(const ImuSample &sample, socket_message_ImuSample &proto) {
    proto.t_us = static_cast<uint64_t>(sample.t_us);
    std::copy(sample.accel.begin(), sample.accel.end(), proto.accel);
    std::copy(sample.gyro.begin(), sample.gyro.end(), proto.gyro);
    std::copy(sample.mag.begin(), sample.mag.end(), proto.mag);
    proto.mag_t_us = static_cast<uint64_t>(sample.mag_t_us);
    std::copy(sample.gravity.begin(), sample.gravity.end(), proto.gravity);
    std::copy(sample.quat.begin(), sample.quat.end(), proto.quat);
    std::copy(sample.rpy.begin(), sample.rpy.end(), proto.rpy);
    proto.temperature = sample.temperature;
    proto.valid = sample.valid;
}

/**
 * Hands tick samples from the control task to the service task. The control task records only while someone
 * listens, never waits, and counts what a full ring refuses; the service task takes whole batches.
 */
class Telemetry {
  public:
    static constexpr size_t RING_TICKS = 64;
    static constexpr size_t BATCH_TICKS = 10;  // socket_message.TelemetryBatch.ticks max_count
    static_assert(BATCH_TICKS == sizeof(socket_message_TelemetryBatch::ticks) / sizeof(socket_message_TickSample));

    void setRecording(bool recording) { _recording.store(recording, std::memory_order_relaxed); }
    bool recording() const { return _recording.load(std::memory_order_relaxed); }

    void record(const socket_message_TickSample &tick) {
        if (!recording()) return;
        if (!_ring.push(tick)) _dropped.fetch_add(1, std::memory_order_relaxed);
    }

    bool takeBatch(socket_message_TelemetryBatch &batch) {
        if (_ring.size() < BATCH_TICKS) return false;
        batch = socket_message_TelemetryBatch_init_zero;
        batch.batch_seq = _batchSeq++;
        batch.dropped_ticks = droppedTicks();
        while (batch.ticks_count < BATCH_TICKS && _ring.pop(batch.ticks[batch.ticks_count])) batch.ticks_count++;
        return true;
    }

    uint32_t droppedTicks() const { return _dropped.load(std::memory_order_relaxed); }

  private:
    SpscRing<socket_message_TickSample, RING_TICKS> _ring;
    std::atomic<bool> _recording {false};
    std::atomic<uint32_t> _dropped {0};
    uint32_t _batchSeq = 0;
};
