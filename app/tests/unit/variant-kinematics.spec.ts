import { describe, it, expect } from 'vitest'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import { variants } from '../../src/lib/stores/featureFlags'

// The firmware's KinConfig is what moves the robot; the app's copy drives the 3D view and gait
// preview, so every variant the firmware knows must exist in the app with the same dimensions.
const firmwareKinematics = () => {
    const header = readFileSync(
        path.resolve(__dirname, '../../../esp32/include/kinematics.h'),
        'utf8'
    )
    // The constructor's float parameters name the dimensions each KIN_CONFIG_<variant> gives in order.
    const parameters = header.match(/constexpr KinConfig\(([^)]*)\)/)?.[1] ?? ''
    const dimensions = [...parameters.matchAll(/float (\w+)/g)].map(([, name]) => name)
    const configs = [...header.matchAll(/constexpr KinConfig KIN_CONFIG_(\w+) \{([^}]*)\};/g)]
    return Object.fromEntries(
        configs.map(([, variant, values]) => {
            const numbers = values.split(',').map(value => value.trim())
            return [
                variant,
                Object.fromEntries(
                    dimensions.map((name, i) => [name, Number(numbers[i].replace(/f$/, ''))])
                )
            ]
        })
    )
}

describe('variant kinematics', () => {
    const firmware = firmwareKinematics()

    it('parses every variant the firmware defines', () => {
        expect(Object.keys(firmware)).toEqual([
            'SPOTMICRO_ESP32',
            'SPOTMICRO_ESP32_MINI',
            'SPOTMICRO_YERTLE'
        ])
    })

    for (const [variant, dimensions] of Object.entries(firmwareKinematics())) {
        it(`matches the firmware dimensions for ${variant}`, () => {
            const app = variants[variant as keyof typeof variants]
            expect(app, `${variant} is missing from the app`).toBeDefined()
            expect(app.kinematics).toEqual(dimensions)
        })
    }
})
