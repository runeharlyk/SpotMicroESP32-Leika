import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import { writable } from 'svelte/store'
import type { RecorderState } from '../../src/lib/recording/recorder'
import { loadRecording } from '../../src/lib/recording/leika-recording'
import { summarize } from '../../src/lib/recording/telemetry-stats'

const state = vi.hoisted(() => ({ store: undefined as unknown }))
vi.mock('$lib/recording/recorder', async () => {
    const { writable } = await import('svelte/store')
    const store = writable({ recording: false, bytes: 0, frames: 0, gaps: 0 })
    state.store = store
    return {
        LARGE_RECORDING_BYTES: 1000,
        recorder: { subscribe: store.subscribe, start: vi.fn(), stop: vi.fn() }
    }
})
vi.mock('$lib/stores/socket', () => ({ socket: writable(true) }))

const { default: TelemetryRecording } =
    await import('../../src/routes/system/metrics/TelemetryRecording.svelte')
const recorderState = state.store as ReturnType<typeof writable<RecorderState>>

const fixture = new Uint8Array(
    readFileSync(path.resolve(__dirname, '../../../simulation/tests/fixtures/app-recording.leika'))
)

describe('the telemetry recording card', () => {
    let component: ReturnType<typeof mount> | undefined
    afterEach(() => component && unmount(component))

    it('warns once a recording grows large', () => {
        recorderState.set({ recording: true, bytes: 2000, frames: 12, gaps: 1 })
        component = mount(TelemetryRecording, { target: document.body })
        flushSync()
        expect(document.body.textContent).toMatch('12 frames, 1 link gaps')
        expect(document.querySelector('[role=alert]')?.textContent).toMatch('stop and download')
    })

    it('shows the report after a recording, and the gyro figures once the robot stood still', () => {
        const recording = loadRecording(fixture)
        const blob = new Blob([fixture])
        recorderState.set({
            recording: false,
            bytes: fixture.length,
            frames: 7,
            gaps: 1,
            finished: {
                blob,
                filename: 'recording-x.leika',
                recording,
                stats: summarize(recording)
            }
        })
        component = mount(TelemetryRecording, { target: document.body })
        flushSync()
        const text = () => document.body.textContent!.replace(/\s+/g, ' ')
        const row = (label: string) =>
            [...document.querySelectorAll('tr')]
                .map(tr => [...tr.cells].map(cell => cell.textContent!.trim()))
                .find(cells => cells[0] === label)
        expect(text()).toMatch('SPOTMICRO_ESP32_MINI app-fixture, IMU MPU6050 at 200 Hz')
        expect(row('Sensor to servo')).toEqual(['Sensor to servo', '6.88', '8.55'])
        expect(row('Reboots')).toEqual(['Reboots', '1'])
        expect(row('Gyro bias y')).toBeUndefined()

        const still = document.querySelector<HTMLInputElement>('input[type=checkbox]')!
        still.click()
        flushSync()
        expect(row('Gyro bias y')).toEqual(['Gyro bias y', '-0.0200 rad/s'])
        expect(row('Roll/pitch noise')).toEqual(['Roll/pitch noise', '0.0595 deg'])
    })
})
