import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import Configuration from '../../src/routes/system/configuration/Configuration.svelte'
import { connectionFeatures } from '../../src/lib/stores/featureFlags'
import { mode } from '../../src/lib/stores/model-store'
import { FeaturesDataResponse, ModeData, ModesEnum } from '../../src/lib/platform_shared/message'
import { PeripheralSettings, ServoSettings } from '../../src/lib/platform_shared/api'
import { serializeConfig } from '../../src/lib/robot-config'
import { fakeRobot } from './fake-robot'

const button = (label: RegExp) =>
    [...document.body.querySelectorAll('button')].find(b => label.test(b.textContent ?? ''))!

const fileFrom = (name: string, deviceId: string) =>
    serializeConfig({
        format: 'leika-robot-config',
        version: 1,
        exportedAt: '2026-10-05T12:00:00.000Z',
        robot: {
            name,
            variant: 'SPOTMICRO_ESP32_MINI',
            deviceId,
            firmwareVersion: '0.3.0',
            buildTarget: 'esp32-s3-n8r2'
        },
        servo: ServoSettings.create({ servos: [{ centerPwm: 310 }] }),
        peripherals: PeripheralSettings.create({ sda: 47, scl: 21 })
    })

async function pick(text: string) {
    const input = document.body.querySelector<HTMLInputElement>('input[type=file]')!
    const file = new File([text], 'pico.json')
    // jsdom's File lacks the text() every browser has.
    Object.defineProperty(file, 'text', { value: async () => text })
    Object.defineProperty(input, 'files', { value: [file], configurable: true })
    input.dispatchEvent(new Event('change', { bubbles: true }))
    await vi.waitFor(() => expect(document.body.textContent).toMatch('saved 2026-10-05'))
}

describe('Configuration page', () => {
    let component: ReturnType<typeof mount> | undefined
    let robot: ReturnType<typeof fakeRobot> | undefined

    afterEach(() => {
        robot?.restore()
        if (component) unmount(component)
        component = undefined
        document.body.innerHTML = ''
    })

    function open() {
        connectionFeatures.set(
            FeaturesDataResponse.create({
                robotName: 'Pico Two',
                deviceId: 'bb22',
                variant: 'SPOTMICRO_ESP32_MINI',
                firmwareBuiltTarget: 'esp32-s3-n8r2'
            })
        )
        mode.set(ModeData.create({ mode: ModesEnum.STAND }))
        component = mount(Configuration, { target: document.body })
        flushSync()
    }

    it('loads another robot calibration, keeping this robot name', async () => {
        robot = fakeRobot(() => ({}))
        open()
        await pick(fileFrom('Pico One', 'aa11'))

        expect(document.body.textContent).toMatch(
            'This file was saved on Pico One; Pico Two keeps its own name.'
        )
        // The variant matches, so loading needs no deactivation.
        button(/^Load$/).click()
        await vi.waitFor(() => expect(document.body.textContent).toMatch('Configuration loaded.'))
        expect(robot.sent).toEqual(['servoSettings', 'peripheralSettings'])
    })

    it('says why a file cannot be read', async () => {
        open()
        const input = document.body.querySelector<HTMLInputElement>('input[type=file]')!
        const file = new File(['{}'], 'other.json')
        Object.defineProperty(file, 'text', { value: async () => '{}' })
        Object.defineProperty(input, 'files', { value: [file], configurable: true })
        input.dispatchEvent(new Event('change', { bubbles: true }))
        await vi.waitFor(() =>
            expect(document.body.textContent).toMatch('This file is not a robot configuration')
        )
    })
})
