#pragma once

#include <motion.h>
#include <peripherals/servo_controller.h>
#include <telemetry/telemetry.h>
#include <algorithm>

/** When a control tick's parts ran, in esp_timer microseconds. */
struct TickTimes {
    uint32_t seq;
    int64_t start;
    int64_t previousStart;  // 0 for the first tick
    int64_t computed;       // just before the servo write
    int64_t written;        // just after it
};

/** One control tick as the simulation needs it; angles leave the robot in radians. */
inline void fillTickSample(socket_message_TickSample &tick, const TickTimes &times, const ImuSample &imu,
                           ServoWrite write, const MotionService &motion, const ServoController &servos) {
    tick = socket_message_TickSample_init_zero;
    tick.seq = times.seq;
    tick.t_us = times.start;
    tick.period_us = times.previousStart ? times.start - times.previousStart : 0;
    tick.compute_us = times.computed - times.start;
    tick.has_imu = true;
    imuToProto(imu, tick.imu);
    const float *targets = motion.getAngles();
    const float *angles = servos.outputAngles();
    const uint16_t *pwm = servos.outputPwm();
    for (int i = 0; i < 12; i++) {
        tick.angles[i] = DEG_TO_RAD_F(angles[i]);
        tick.targets[i] = DEG_TO_RAD_F(targets[i]);
        tick.pwm[i] = pwm[i];
    }
    tick.servo_write_us = write.attempted ? times.written - times.computed : 0;
    tick.servo_ok = write.ok;
    const CommandMsg &command = motion.currentCommand();
    const float values[7] = {command.lx, command.ly, command.rx, command.ry, command.h, command.s, command.s1};
    std::copy(values, values + 7, tick.command);
    tick.command_rx_us = motion.commandReceivedAt();
    tick.command_age_us = tick.command_rx_us ? times.start - static_cast<int64_t>(tick.command_rx_us) : 0;
    tick.mode = motion.mode();
    tick.gait = motion.gait();
    tick.link_lost = motion.linkLost();
}
