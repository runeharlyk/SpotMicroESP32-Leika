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
        ws2812: { enabled: true, pin: 12 },
        leftSonar: { enabled: true, triggerPin: 1, echoPin: 2 },
        rightSonar: { enabled: false, triggerPin: -1, echoPin: -1 },
        powerSensor: {
            address: 0x41,
            shuntOhms: 0.1,
            maxCurrentA: 3.2,
            cutoffVoltage: 0,
            stallCurrentA: 3,
            stallTimeMs: 1000
        }
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
            ['power', 'absent'],
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
            'power',
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
            power: true,
            camera: true
        })
        expect(form.espnowController).toBe(false)

        // Every flag flips, so one left out of the message would keep its stored value and show.
        form.use.imu = false
        form.use.mag = true
        form.use.bmp = true
        form.use.gesture = false
        form.use.power = false
        form.use.camera = false
        form.ws2812!.pin = 13
        form.espnowController = true
        form.rightSonar = { enabled: true, triggerPin: 3, echoPin: 34 }
        form.powerSensor!.cutoffVoltage = 6.5
        form.powerSensor!.address = 0x44

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
            powerDisabled: true,
            cameraDisabled: true,
            ws2812: { enabled: true, pin: 13 },
            rightSonar: { enabled: true, triggerPin: 3, echoPin: 34 },
            powerSensor: {
                ...stored().powerSensor,
                shuntOhms: expect.closeTo(0.1, 6),
                maxCurrentA: expect.closeTo(3.2, 6),
                cutoffVoltage: 6.5,
                address: 0x44
            },
            espnowController: true
        })
    })

    it('leaves the stored sonars alone when the robot reported none', () => {
        const settings = { ...stored(), leftSonar: undefined, rightSonar: undefined }
        const saved = settingsFromForm(settings, formFromSettings(settings))

        expect(saved.leftSonar).toBeUndefined()
        expect(saved.rightSonar).toBeUndefined()
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

    it('refuses an enabled sonar without pins, on one pin, or on a pin something else uses', () => {
        const settings = stored()
        const form = formFromSettings(settings)
        const left = form.leftSonar!
        expect(formProblem(settings, form)).toBeNull()

        left.echoPin = null as unknown as number
        expect(formProblem(settings, form)).toMatch(/left sonar needs a trigger and an echo/)
        left.echoPin = 1
        expect(formProblem(settings, form)).toMatch(/left sonar needs its trigger and echo on two/)
        left.echoPin = 22
        expect(formProblem(settings, form)).toMatch(/left sonar and I2C SCL both use pin 22/)
        left.echoPin = 12
        expect(formProblem(settings, form)).toMatch(/left sonar and the WS2812 LEDs .* 12/)
        form.ws2812!.enabled = false
        expect(formProblem(settings, form)).toBeNull()

        form.rightSonar = { enabled: true, triggerPin: 12, echoPin: 5 }
        expect(formProblem(settings, form)).toMatch(/two sonars both use pin 12/)
        form.rightSonar.triggerPin = 6
        expect(formProblem(settings, form)).toBeNull()
    })

    it('reports the LEDs before the sonars, and a right sonar on its own', () => {
        const settings = stored()
        const form = formFromSettings(settings)

        form.ws2812!.pin = 21
        form.leftSonar!.triggerPin = 22
        expect(formProblem(settings, form)).toMatch(/WS2812 LEDs and I2C SDA/)
        form.ws2812!.pin = 12
        form.leftSonar!.triggerPin = 1
        form.rightSonar = { enabled: true, triggerPin: 21, echoPin: 4 }
        expect(formProblem(settings, form)).toMatch(/right sonar and I2C SDA both use pin 21/)
    })

    it('lets disabled sonars keep pins that clash', () => {
        const settings = stored()
        const form = formFromSettings(settings)

        form.leftSonar = { enabled: false, triggerPin: 21, echoPin: 21 }
        form.rightSonar = { enabled: false, triggerPin: 12, echoPin: 21 }

        expect(formProblem(settings, form)).toBeNull()
    })

    it('leaves the stored power sensor alone when the robot reported none', () => {
        const settings = { ...stored(), powerSensor: undefined }

        expect(settingsFromForm(settings, formFromSettings(settings)).powerSensor).toBeUndefined()
    })

    it('refuses power sensor settings the robot would refuse', () => {
        const settings = stored()
        const form = formFromSettings(settings)
        const power = form.powerSensor!
        expect(formProblem(settings, form)).toBeNull()

        power.address = 0x40
        expect(formProblem(settings, form)).toMatch(/address from 0x41 to 0x4F/)
        power.address = 0x4f
        expect(formProblem(settings, form)).toBeNull()

        power.shuntOhms = 0
        expect(formProblem(settings, form)).toMatch(/shunt resistance above 0/)
        // A cleared number input binds null.
        power.shuntOhms = null as unknown as number
        expect(formProblem(settings, form)).toMatch(/shunt resistance above 0/)
        power.shuntOhms = 0.1

        power.maxCurrentA = 3.3
        expect(formProblem(settings, form)).toMatch(/more than 0.32 V across the shunt/)
        power.shuntOhms = 0.01
        power.maxCurrentA = 20
        expect(formProblem(settings, form)).toBeNull()

        power.cutoffVoltage = 27
        expect(formProblem(settings, form)).toMatch(/cut-off voltage/)
        power.cutoffVoltage = 6.4
        power.stallCurrentA = -1
        expect(formProblem(settings, form)).toMatch(/stall current/)
        power.stallCurrentA = 0
        power.stallTimeMs = 0.5
        expect(formProblem(settings, form)).toMatch(/stall time/)
        power.stallTimeMs = 0
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

    it('saves a sonar switched on with its pins, and holds back a clash', async () => {
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
        await vi.waitFor(() =>
            expect(document.body.querySelector('[aria-label="Use the right sonar"]')).not.toBeNull()
        )
        const input = (label: string) =>
            document.body.querySelector<HTMLInputElement>(`[aria-label="${label}"]`)!
        const type = (label: string, value: string) => {
            input(label).value = value
            input(label).dispatchEvent(new Event('input', { bubbles: true }))
            flushSync()
        }
        const saveButton = () =>
            [...document.body.querySelectorAll('button')].find(b => /Save/.test(b.textContent!))!

        expect(input('Right sonar trigger pin').disabled).toBe(true)
        input('Use the right sonar').click()
        flushSync()
        type('Right sonar trigger pin', '2')
        type('Right sonar echo pin', '5')
        // Pin 2 is the left sonar's echo.
        expect(document.body.querySelector('[role="alert"]')!.textContent).toMatch(
            /two sonars both use pin 2/
        )
        expect(saveButton().disabled).toBe(true)

        type('Right sonar trigger pin', '4')
        expect(document.body.querySelector('[role="alert"]')).toBeNull()
        saveButton().click()

        await vi.waitFor(() => expect(saves).toHaveLength(1))
        expect(saves[0]).toMatchObject({
            leftSonar: { enabled: true, triggerPin: 1, echoPin: 2 },
            rightSonar: { enabled: true, triggerPin: 4, echoPin: 5 }
        })
    })

    it('saves the power sensor at another address with a cut-off, and holds back too large a current', async () => {
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
        await vi.waitFor(() =>
            expect(document.body.querySelector('[aria-label="Cut-off voltage"]')).not.toBeNull()
        )
        const field = (label: string) =>
            document.body.querySelector<HTMLInputElement | HTMLSelectElement>(
                `[aria-label="${label}"]`
            )!
        const set = (label: string, value: string, event: string) => {
            field(label).value = value
            field(label).dispatchEvent(new Event(event, { bubbles: true }))
            flushSync()
        }
        const saveButton = () =>
            [...document.body.querySelectorAll('button')].find(b => /Save/.test(b.textContent!))!

        const address = field('Power sensor address') as HTMLSelectElement
        // 0x40 is the servo board's.
        expect([...address.options].map(option => option.textContent)).not.toContain('0x40')
        address.selectedIndex = [...address.options].findIndex(o => o.textContent === '0x44')
        address.dispatchEvent(new Event('change', { bubbles: true }))
        flushSync()
        set('Cut-off voltage', '6.5', 'input')
        set('Maximum current in amperes', '4', 'input')
        expect(document.body.querySelector('[role="alert"]')!.textContent).toMatch(/0.32 V/)
        expect(saveButton().disabled).toBe(true)

        set('Maximum current in amperes', '3', 'input')
        saveButton().click()
        await vi.waitFor(() => expect(saves).toHaveLength(1))
        expect(saves[0].powerSensor).toMatchObject({
            address: 0x44,
            cutoffVoltage: 6.5,
            maxCurrentA: 3,
            stallTimeMs: 1000
        })
    })

    const mountWith = async (features: FeaturesDataResponse, saves: PeripheralSettings[] = []) => {
        robot = fakeRobot((name, data) => {
            if (name === 'featuresDataRequest') return { featuresDataResponse: features }
            if (name === 'peripheralSettings') {
                saves.push(data.peripheralSettings!)
                return { peripheralSettings: data.peripheralSettings }
            }
            return { peripheralSettings: stored() }
        })
        component = mount(Sensors, { target: document.body })
        await vi.waitFor(() => expect(document.body.querySelector('tbody tr')).not.toBeNull())
    }

    const controllerToggle = () =>
        [...document.body.querySelectorAll('label')]
            .find(label => /ESP-NOW controller/.test(label.textContent!))
            ?.querySelector<HTMLInputElement>('input')

    it('offers no ESP-NOW controller on a board without a radio for it', async () => {
        await mountWith(reported())

        expect(controllerToggle()).toBeUndefined()
    })

    it('saves the ESP-NOW controller switched on', async () => {
        const saves: PeripheralSettings[] = []
        await mountWith({ ...reported(), espnowControllerSupported: true }, saves)

        const toggle = controllerToggle()!
        expect(toggle.checked).toBe(false)
        toggle.click()
        flushSync()
        ;[...document.body.querySelectorAll('button')]
            .find(b => /Save/.test(b.textContent!))!
            .click()

        await vi.waitFor(() => expect(saves).toHaveLength(1))
        expect(saves[0]).toMatchObject({ espnowController: true, magDisabled: true })
    })
})
