// @vitest-environment node
import { describe, it, expect } from 'vitest'
import { readFileSync, writeFileSync } from 'node:fs'
import path from 'node:path'
import {
    RecordingWriter,
    loadRecording,
    readRecords
} from '../../src/lib/recording/leika-recording'
import { summarize } from '../../src/lib/recording/telemetry-stats'
import {
    Message,
    TelemetryBatch,
    TelemetryHeader,
    TickSample
} from '../../src/lib/platform_shared/message'

const fixtures = path.resolve(__dirname, '../../../simulation/tests/fixtures')
const pythonFixture = new Uint8Array(readFileSync(path.join(fixtures, 'telemetry.leika')))
// Written by the app's writer below and read by simulation/tests/test_app_recording.py, which checks its own
// summary against the stats file: `UPDATE_FIXTURES=1 pnpm vitest run leika-recording` writes both anew.
const appFixturePath = path.join(fixtures, 'app-recording.leika')
const appStatsPath = path.join(fixtures, 'app-recording.stats.json')

const bytesOf = async (writer: RecordingWriter) => new Uint8Array(await writer.blob().arrayBuffer())
const frame = (message: Partial<Message>) => Message.encode(Message.create(message)).finish()

function batch(
    batchSeq: number,
    firstTick: number,
    droppedSinceBoot: number,
    tick: (seq: number) => Partial<TickSample> = () => ({})
) {
    return frame({
        telemetryBatch: TelemetryBatch.create({
            batchSeq,
            droppedTicks: droppedSinceBoot,
            ticks: Array.from({ length: 10 }, (_, i) =>
                TickSample.create({ seq: firstTick + i, ...tick(firstTick + i) })
            )
        })
    })
}

async function recordingOf(frames: Uint8Array<ArrayBuffer>[]) {
    const writer = new RecordingWriter()
    frames.forEach(f => writer.write(f, 1))
    return loadRecording(await bytesOf(writer))
}

/** A recording whose every timing varies, so percentiles interpolate and spreads are not zero. */
function variedTick(seq: number): Partial<TickSample> {
    const tUs = 5_000_000 + seq * 10_000
    const hasImu = seq % 7 !== 3
    return {
        tUs,
        periodUs: seq % 11 === 0 ? 0 : 10_000 + ((seq * 373) % 900) - 450,
        computeUs: 1_200 + ((seq * 131) % 700),
        servoWriteUs: seq % 9 === 4 ? 0 : 1_800 + ((seq * 59) % 500),
        servoOk: seq % 13 !== 5,
        commandRxUs: seq < 5 ? 0 : tUs - 20_000,
        commandAgeUs: seq < 5 ? 0 : 15_000 + ((seq * 977) % 9_000),
        imu: {
            tUs: tUs - 2_000 - ((seq * 211) % 3_000),
            accel: [0, 0, 9.81],
            gyro: [
                0.01 + 0.003 * Math.sin(seq),
                -0.02 + 0.002 * Math.cos(seq * 1.7),
                0.004 * Math.sin(seq * 0.3)
            ],
            mag: [],
            magTUs: 0,
            gravity: [0, 0, -1],
            quat: [1, 0, 0, 0],
            rpy: [0.002 * Math.sin(seq * 2.1), 0.001 * Math.cos(seq * 0.9), 0.5],
            temperature: 30,
            valid: hasImu ? 0b1011 : 0b1000
        }
    }
}

async function appFixture() {
    const writer = new RecordingWriter()
    let hostUs = 1_000_000
    const write = (f: Uint8Array<ArrayBuffer>) => writer.write(f, (hostUs += 100_000))
    write(
        frame({
            telemetryHeader: TelemetryHeader.create({
                firmwareVersion: 'app-fixture',
                buildTarget: 'esp32-s3-n8r2',
                variant: 'SPOTMICRO_ESP32_MINI',
                deviceId: 'leika-test',
                imuDriver: 'MPU6050',
                imuRateHz: 200,
                magRateHz: 0,
                controlRateHz: 100,
                batchTicks: 10,
                jointDirection: [1, -1, -1, 1, -1, -1, 1, 1, 1, 1, 1, 1]
            })
        })
    )
    write(batch(40, 400, 5, variedTick))
    write(batch(41, 410, 7, variedTick))
    write(
        frame({
            telemetryNetwork: {
                tUs: 5_200_000,
                rssi: -61,
                channel: 6,
                batchesSent: 41,
                batchesFailed: 1,
                ticksDropped: 7
            }
        })
    )
    writer.gap((hostUs += 2_000_000))
    write(batch(43, 430, 9, variedTick)) // batch 42 was lost with the link
    write(batch(0, 0, 2, variedTick)) // the robot rebooted
    return bytesOf(writer)
}

describe('the .leika recording', () => {
    it('reads the recorder fixture with the numbers test_report.py expects', () => {
        const stats = summarize(loadRecording(pythonFixture))
        expect(stats.period_ms_mean).toBeCloseTo(10.0, 9)
        expect(stats.imu_age_ms_p50).toBeCloseTo(3.0, 9)
        expect(stats.sensor_to_servo_ms_p50).toBeCloseTo(6.5, 9)
        expect(stats.command_age_ms_p50).toBeCloseTo(20.0, 9)
        expect(stats.servo_ok_fraction).toBeCloseTo(1.0, 9)
        expect(stats.gyro_bias_x).toBeCloseTo(0.01, 6)
        expect(stats.gyro_bias_y).toBeCloseTo(-0.02, 6)
        expect(stats.gyro_noise_std).toBeCloseTo(0.0, 6)
        expect(stats.commanded_ticks).toBe(25) // ticks 5-19 and 30-39; batch 20-29 was lost
        expect(stats).toMatchObject({ ticks: 30, missing_ticks: 10, link_gaps: 1, reboots: 0 })
    })

    it('writes the recorder fixture byte for byte when given its frames', async () => {
        const writer = new RecordingWriter()
        for (const [hostRxUs, f] of readRecords(pythonFixture)) {
            if (f.length) writer.write(new Uint8Array(f), hostRxUs)
            else writer.gap(hostRxUs)
        }
        expect(await bytesOf(writer)).toEqual(pythonFixture)
    })

    it('counts a reboot mid-recording as neither missing ticks nor earlier drops', async () => {
        const rec = await recordingOf([
            batch(500, 5000, 40),
            batch(502, 5020, 43), // batch 501 lost
            batch(0, 0, 0), // rebooted
            batch(1, 10, 2)
        ])
        expect(rec.reboots).toBe(1)
        expect(rec.missingBatches).toBe(1)
        expect(rec.missingTicks).toBe(10)
        expect(rec.droppedTicks).toBe(3 + 2)
    })

    it('summarizes a recording without ticks', async () => {
        const stats = summarize(await recordingOf([]))
        expect(stats).toEqual({
            ticks: 0,
            missing_ticks: 0,
            dropped_ticks: 0,
            link_gaps: 0,
            reboots: 0
        })
    })

    it('keeps every record before one the recorder stopped in the middle of', async () => {
        const writer = new RecordingWriter()
        writer.write(batch(1, 10, 0), 1)
        writer.write(batch(2, 20, 0), 2)
        const bytes = await bytesOf(writer)
        expect(loadRecording(bytes.subarray(0, bytes.length - 5)).ticks.map(t => t.seq)).toEqual(
            Array.from({ length: 10 }, (_, i) => 10 + i)
        )
    })

    it('refuses a file that is not a recording', () => {
        expect(() => loadRecording(new TextEncoder().encode('LEIKAREX\x01\x00'))).toThrow(
            'not a Leika recording'
        )
    })

    it('matches the fixture the Python loader checks its summary against', async () => {
        const bytes = await appFixture()
        const stats = summarize(loadRecording(bytes))
        if (process.env.UPDATE_FIXTURES) {
            writeFileSync(appFixturePath, bytes)
            writeFileSync(appStatsPath, JSON.stringify(stats, null, 4) + '\n')
        }
        expect(bytes).toEqual(new Uint8Array(readFileSync(appFixturePath)))
        expect(stats).toEqual(JSON.parse(readFileSync(appStatsPath, 'utf8')))
        expect(stats).toMatchObject({
            ticks: 40,
            missing_ticks: 10,
            dropped_ticks: 4 + 2,
            link_gaps: 1,
            reboots: 1,
            commanded_ticks: 35
        })
    })
})
