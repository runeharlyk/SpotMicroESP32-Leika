import { describe, it, expect, afterEach, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import Sensors from '../../src/routes/peripherals/sensors/Sensors.svelte'
import {
    formFromSettings,
    formProblem,
    sensorRows,
    settingsFromForm
} from '../../src/lib/peripherals'
import { PeripheralSettings } from '../../src/lib/platform_shared/api'
import { FeaturesDataResponse } from '../../src/lib/platform_shared/message'
import { fakeRobot } from './fake-robot'

const stored = () =>
    PeripheralSettings.create({
        sda: 21,
        scl: 22,
        frequency: 400000,
        pins: [{ pin: 4, mode: 'output', type: 'digital', role: 'buzzer' }],
        imu: { mounting: [0, 1, 0, -1, 0, 0, 0, 0, 1], fusionGain: 0.25 },
        magDisabled: true,
        bmpDisabled: true,
        ws2812: { enabled: true, pin: 12 }
    })

// The magnetometer answers but is switched off; the barometer is switched off and so never probed.
const reported = () =>
    FeaturesDataResponse.create({
        variant: 'SPOTMICRO_ESP32_MINI',
        imu: true,
        imuDetected: true,
        magDetected: true,
        camera: true,
        cameraDetected: true,
        servoDetected: true,
        servo: true
    })

describe('sensor rows', () => {
    it('tells a detected but disabled sensor from a disabled unprobed one and an absent one', () => {
        const rows = sensorRows(reported(), stored())

        expect(rows.map(row => [row.sensor, row.status])).toEqual([
            ['imu', 'active'],
            ['mag', 'detected-but-disabled'],
            ['bmp', 'disabled'],
            ['gesture', 'absent'],
            ['camera', 'active'],
            ['servo', 'active']
        ])
    })

    it('offers no toggle for the servo board, which cannot be disabled', () => {
        const rows = sensorRows(reported(), stored())

        expect(rows.find(row => row.sensor === 'servo')?.toggle).toBeNull()
        expect(rows.filter(row => row.toggle).map(row => row.toggle)).toEqual([
            'imu',
            'mag',
            'bmp',
            'gesture',
            'camera'
        ])
    })

    it('shows a servo board that did not answer as absent', () => {
        const features = { ...reported(), servo: false, servoDetected: false }

        expect(sensorRows(features, stored()).find(row => row.sensor === 'servo')?.status).toBe(
            'absent'
        )
    })
})

describe('peripheral settings form', () => {
    it('sends every disabled flag and the edited outputs, and keeps the I2C and IMU settings', () => {
        const settings = stored()
        const form = formFromSettings(settings)
        expect(form.use).toEqual({
            imu: true,
            mag: false,
            bmp: false,
            gesture: true,
            camera: true
        })

        // Every flag flips, so one left out of the message would keep its stored value and show.
        form.use.imu = false
        form.use.mag = true
        form.use.bmp = true
        form.use.gesture = false
        form.use.camera = false
        form.ws2812!.pin = 13

        // Through the wire format, as the robot will read it.
        const saved = PeripheralSettings.decode(
            PeripheralSettings.encode(settingsFromForm(settings, form)).finish()
        )
        expect(saved).toEqual({
            ...stored(),
            imuDisabled: true,
            magDisabled: false,
            bmpDisabled: false,
            gestureDisabled: true,
            cameraDisabled: true,
            ws2812: { enabled: true, pin: 13 }
        })
    })

    it('leaves the stored LED settings alone when the robot reported none', () => {
        const settings = { ...stored(), ws2812: undefined }

        expect(settingsFromForm(settings, formFromSettings(settings)).ws2812).toBeUndefined()
    })

    it('does not change the received settings while the form is edited', () => {
        const settings = stored()
        const form = formFromSettings(settings)

        form.ws2812!.pin = 5

        expect(settings.ws2812!.pin).toBe(12)
    })

    it('refuses enabled LEDs on an I2C pin or without a pin', () => {
        const settings = stored()
        const form = formFromSettings(settings)

        form.ws2812!.pin = 21
        expect(formProblem(settings, form)).toMatch(/I2C SDA.*21/)
        form.ws2812!.pin = 22
        expect(formProblem(settings, form)).toMatch(/I2C SCL.*22/)
        // A cleared number input binds null.
        form.ws2812!.pin = null as unknown as number
        expect(formProblem(settings, form)).toMatch(/need a pin/)
        form.ws2812!.pin = 0
        expect(formProblem(settings, form)).toBeNull()
    })

    it('lets disabled LEDs keep a pin that clashes', () => {
        const settings = stored()
        const form = formFromSettings(settings)

        form.ws2812 = { enabled: false, pin: 21 }

        expect(formProblem(settings, form)).toBeNull()
    })
})

describe('Sensors page', () => {
    let component: ReturnType<typeof mount> | undefined
    let robot: ReturnType<typeof fakeRobot> | undefined

    afterEach(() => {
        if (component) unmount(component)
        robot?.restore()
        document.body.innerHTML = ''
    })

    it('shows a detected but disabled sensor and saves its toggle as a disabled flag', async () => {
        const saves: PeripheralSettings[] = []
        robot = fakeRobot((name, data) => {
            if (name === 'featuresDataRequest') return { featuresDataResponse: reported() }
            if (name === 'peripheralSettings') {
                saves.push(data.peripheralSettings!)
                return { peripheralSettings: data.peripheralSettings }
            }
            return { peripheralSettings: stored() }
        })
        component = mount(Sensors, { target: document.body })
        await vi.waitFor(() => expect(document.body.querySelector('tbody tr')).not.toBeNull())

        const row = (label: string) =>
            [...document.body.querySelectorAll('tbody tr')].find(
                tr => tr.querySelector('td')?.textContent === label
            )!
        expect(row('Magnetometer').textContent).toMatch(/Detected but disabled/)
        expect(row('Barometer').textContent).toMatch(/Disabled/)
        expect(row('Servo board').querySelector('input')).toBeNull()
        // The camera driver is not torn down live; the others are.
        expect(row('Camera').textContent).toMatch(/after a restart/)
        expect(row('IMU').textContent).not.toMatch(/restart/)

        const magToggle = row('Magnetometer').querySelector<HTMLInputElement>('input')!
        expect(magToggle.checked).toBe(false)
        magToggle.click()
        flushSync()
        ;[...document.body.querySelectorAll('button')]
            .find(b => /Save/.test(b.textContent!))!
            .click()

        await vi.waitFor(() => expect(saves).toHaveLength(1))
        expect(saves[0]).toMatchObject({
            sda: 21,
            scl: 22,
            frequency: 400000,
            imuDisabled: false,
            magDisabled: false,
            bmpDisabled: true,
            ws2812: { enabled: true, pin: 12 }
        })
        // The robot re-probes after a save, so what it detected is fetched again.
        await vi.waitFor(() =>
            expect(robot!.sent.filter(name => name === 'featuresDataRequest')).toHaveLength(2)
        )
    })
})
