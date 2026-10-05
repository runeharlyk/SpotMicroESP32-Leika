import { afterEach, describe, expect, it } from 'vitest'
import { fakeRobot } from './fake-robot'
import { FeaturesDataResponse } from '../../src/lib/platform_shared/message'
import { PeripheralSettings, ServoSettings } from '../../src/lib/platform_shared/api'
import {
    applyConfig,
    exportConfig,
    parseConfig,
    planImport,
    serializeConfig,
    type RobotConfig
} from '../../src/lib/robot-config'

const pico = FeaturesDataResponse.create({
    robotName: 'Pico One',
    variant: 'SPOTMICRO_ESP32_MINI',
    deviceId: 'aa11',
    firmwareVersion: '0.3.0',
    firmwareBuiltTarget: 'esp32-s3-n8r2'
})
const servo = ServoSettings.create({
    servos: Array.from({ length: 12 }, (_, i) => ({ centerPwm: 300 + i, name: `joint ${i}` })),
    channels: [3, 2, 1, 0, 4, 5, 6, 7, 8, 9, 10, 11],
    model: { direction: Array(12).fill(1), centerAngle: Array(12).fill(0), pwmPerDegree: 2 }
})
const peripherals = PeripheralSettings.create({
    sda: 47,
    scl: 21,
    frequency: 400000,
    imu: { magOffset: [1.5, -2, 3], fusionGain: 0.1 },
    ws2812: { enabled: true, pin: 48 }
})

const saved = (overrides: Partial<RobotConfig['robot']> = {}): RobotConfig => ({
    format: 'leika-robot-config',
    version: 1,
    exportedAt: '2026-10-05T12:00:00.000Z',
    robot: {
        name: 'Pico One',
        variant: 'SPOTMICRO_ESP32_MINI',
        deviceId: 'aa11',
        firmwareVersion: '0.3.0',
        buildTarget: 'esp32-s3-n8r2',
        ...overrides
    },
    servo: ServoSettings.create({ ...servo, model: undefined }),
    peripherals
})

describe('robot configuration', () => {
    let robot: ReturnType<typeof fakeRobot> | undefined

    afterEach(() => robot?.restore())

    it('exports the calibration and peripheral settings, without the variant joint model', async () => {
        robot = fakeRobot(name => {
            if (name === 'featuresDataRequest') return { featuresDataResponse: pico }
            if (name === 'servoSettingsRequest') return { servoSettings: servo }
            return { peripheralSettings: peripherals }
        })

        const config = await exportConfig(new Date('2026-10-05T12:00:00Z'))

        expect(config).toEqual(saved())
        expect(config.servo.model).toBeUndefined()
    })

    it('reads back exactly what it wrote', () => {
        expect(parseConfig(serializeConfig(saved()))).toEqual(saved())
    })

    it('refuses a file that is not a robot configuration', () => {
        expect(() => parseConfig('not json')).toThrow('not a robot configuration')
        expect(() => parseConfig(JSON.stringify({ hello: 1 }))).toThrow('not a robot configuration')
        expect(() =>
            parseConfig(serializeConfig(saved()).replace('"version": 1', '"version": 2'))
        ).toThrow('version 2')
    })

    it('restores the name only on the robot the file came from', () => {
        const renamed = FeaturesDataResponse.create({ ...pico, robotName: 'Renamed' })
        expect(planImport(saved(), renamed)).toEqual({ rename: 'Pico One', warnings: [] })

        const other = FeaturesDataResponse.create({
            ...pico,
            deviceId: 'bb22',
            robotName: 'Pico Two'
        })
        const plan = planImport(saved(), other)
        expect(plan.rename).toBeUndefined()
        expect(plan.warnings).toEqual([
            'This file was saved on Pico One; Pico Two keeps its own name.'
        ])
    })

    it('switches the variant when the file is for another one, and warns about another board', () => {
        const yertle = FeaturesDataResponse.create({
            ...pico,
            variant: 'SPOTMICRO_YERTLE',
            firmwareBuiltTarget: 'esp32-wroom-camera'
        })
        const plan = planImport(saved(), yertle)
        expect(plan.variant).toBe('SPOTMICRO_ESP32_MINI')
        expect(plan.warnings).toContain(
            'The file was saved on an esp32-s3-n8r2 board; its pins may not fit this esp32-wroom-camera.'
        )
    })

    it('applies the variant first, then the settings, then the name', async () => {
        robot = fakeRobot(() => ({}))

        await applyConfig(saved(), {
            variant: 'SPOTMICRO_ESP32_MINI',
            rename: 'Pico One',
            warnings: []
        })

        expect(robot.sent).toEqual([
            'robotVariantUpdate',
            'servoSettings',
            'peripheralSettings',
            'robotNameUpdate'
        ])
    })

    it('stops at the first refusal with the robot reason', async () => {
        robot = fakeRobot(name =>
            name === 'robotVariantUpdate' ?
                { statusCode: 409, errorMessage: 'Deactivate first' }
            :   {}
        )

        await expect(
            applyConfig(saved(), { variant: 'SPOTMICRO_ESP32_MINI', warnings: [] })
        ).rejects.toThrow('Deactivate the robot before changing its variant')
        expect(robot.sent).toEqual(['robotVariantUpdate'])
    })
})
