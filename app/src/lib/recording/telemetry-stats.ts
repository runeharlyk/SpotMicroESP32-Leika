import type { TickSample } from '$lib/platform_shared/message'
import type { Recording } from './leika-recording'

/** The statistics of simulation/src/sim/telemetry_stats.py, under the same names, so the two can be compared. */
export type TelemetryStats = Record<string, number>

// The parts of computeUs and of servoWriteUs a tick carries, under telemetry_stats.py's names.
const computeParts: [name: string, us: (tick: TickSample) => number][] = [
    ['motion', t => t.motionUs],
    ['motion_step', t => t.motionStepUs],
    ['odometry', t => t.odometryUs],
    ['goal', t => t.goalUs],
    ['kinematics', t => t.kinematicsUs]
]
const servoParts: [name: string, us: (tick: TickSample) => number][] = [
    ['servo_bus_wait', t => t.servoBusWaitUs],
    ['servo_transfer', t => t.servoTransferUs]
]

const mean = (values: number[]) => values.reduce((sum, value) => sum + value, 0) / values.length

/** Population standard deviation, as NumPy's std. */
const std = (values: number[]) => {
    const centre = mean(values)
    return Math.sqrt(mean(values.map(value => (value - centre) ** 2)))
}

/** NumPy's default percentile of ascending `sorted`: linear interpolation between the closest ranks. */
function percentile(sorted: number[], p: number) {
    const rank = (p / 100) * (sorted.length - 1)
    const below = Math.floor(rank)
    const above = Math.min(below + 1, sorted.length - 1)
    return sorted[below] + (rank - below) * (sorted[above] - sorted[below])
}

function percentiles(name: string, valuesMs: number[], stats: TelemetryStats) {
    if (valuesMs.length === 0) return
    const sorted = [...valuesMs].sort((a, b) => a - b)
    stats[`${name}_ms_mean`] = mean(valuesMs)
    stats[`${name}_ms_p50`] = percentile(sorted, 50)
    stats[`${name}_ms_p99`] = percentile(sorted, 99)
    stats[`${name}_ms_max`] = sorted[sorted.length - 1]
}

const degrees = (radians: number) => (radians * 180) / Math.PI

export function summarize(rec: Recording): TelemetryStats {
    const ticks = rec.ticks
    const stats: TelemetryStats = {
        ticks: ticks.length,
        missing_ticks: rec.missingTicks,
        dropped_ticks: rec.droppedTicks,
        link_gaps: rec.linkGaps,
        reboots: rec.reboots
    }
    if (ticks.length === 0) return stats
    const periods = ticks.filter(t => t.periodUs > 0).map(t => t.periodUs / 1000)
    percentiles('period', periods, stats)
    if (periods.length) stats.period_ms_std = std(periods)
    percentiles(
        'compute',
        ticks.map(t => t.computeUs / 1000),
        stats
    )
    const written = ticks.filter(t => t.servoWriteUs > 0)
    percentiles(
        'servo_write',
        written.map(t => t.servoWriteUs / 1000),
        stats
    )
    computeParts.forEach(([name, us]) =>
        percentiles(
            name,
            ticks.map(t => us(t) / 1000),
            stats
        )
    )
    servoParts.forEach(([name, us]) =>
        percentiles(
            name,
            written.map(t => us(t) / 1000),
            stats
        )
    )
    stats.servo_ok_fraction = written.length ? mean(written.map(t => (t.servoOk ? 1 : 0))) : 0
    const withImu = ticks.filter(t => ((t.imu?.valid ?? 0) & 1) !== 0)
    const imuAge = withImu.map(t => (t.tUs - t.imu!.tUs) / 1000)
    percentiles('imu_age', imuAge, stats)
    percentiles(
        'sensor_to_servo',
        withImu.map((t, i) => imuAge[i] + (t.computeUs + t.servoWriteUs) / 1000),
        stats
    )
    const commanded = ticks.filter(t => t.commandRxUs > 0)
    stats.commanded_ticks = commanded.length
    percentiles(
        'command_age',
        commanded.map(t => t.commandAgeUs / 1000),
        stats
    )
    if (withImu.length) {
        const gyroAxis = (axis: number) => withImu.map(t => t.imu!.gyro[axis])
        const rpyAxis = (axis: number) => withImu.map(t => t.imu!.rpy[axis])
        ;['x', 'y', 'z'].forEach(
            (name, axis) => (stats[`gyro_bias_${name}`] = mean(gyroAxis(axis)))
        )
        stats.gyro_noise_std = mean([0, 1, 2].map(axis => std(gyroAxis(axis))))
        stats.rpy_noise_deg = degrees(mean([0, 1].map(axis => std(rpyAxis(axis)))))
    }
    return stats
}
