import { robotRequest } from '$lib/robot-request'
import { chooseVariant } from '$lib/services/robot-variant'
import { knownVariant, type Variant } from '$lib/kinematics-variants'
import { PeripheralSettings, ServoSettings } from '$lib/platform_shared/api'
import type { FeaturesDataResponse } from '$lib/platform_shared/message'

const FORMAT = 'leika-robot-config'
const VERSION = 1

/**
 * What makes a robot this robot, saved to a file: its servo calibration and its peripheral settings (IMU mounting,
 * compass calibration, pins, LED strip). Network settings are left out: the robot never sends its passwords.
 */
export interface RobotConfig {
    format: typeof FORMAT
    version: number
    exportedAt: string
    robot: {
        name: string
        variant: string
        deviceId: string
        firmwareVersion: string
        buildTarget: string
    }
    servo: ServoSettings
    peripherals: PeripheralSettings
}

/** What importing a file into the connected robot changes besides its settings, and what to tell first. */
export interface ImportPlan {
    variant?: Variant
    rename?: string
    warnings: string[]
}

export async function exportConfig(now = new Date()): Promise<RobotConfig> {
    const features = (await robotRequest({ featuresDataRequest: {} })).featuresDataResponse
    const servo = (await robotRequest({ servoSettingsRequest: {} })).servoSettings
    const peripherals = (await robotRequest({ peripheralSettingsRequest: {} })).peripheralSettings
    if (!features || !servo || !peripherals)
        throw new Error('The robot did not send all of its settings')
    return {
        format: FORMAT,
        version: VERSION,
        exportedAt: now.toISOString(),
        robot: {
            name: features.robotName,
            variant: features.variant,
            deviceId: features.deviceId,
            firmwareVersion: features.firmwareVersion,
            buildTarget: features.firmwareBuiltTarget
        },
        // The joint model is the variant's, sent along for drawing; the robot does not store it.
        servo: ServoSettings.create({ ...servo, model: undefined }),
        peripherals
    }
}

export function serializeConfig(config: RobotConfig): string {
    return JSON.stringify(
        {
            ...config,
            servo: ServoSettings.toJSON(config.servo),
            peripherals: PeripheralSettings.toJSON(config.peripherals)
        },
        null,
        2
    )
}

export function parseConfig(text: string): RobotConfig {
    let file: Record<string, unknown>
    try {
        file = JSON.parse(text)
    } catch {
        throw new Error('This file is not a robot configuration')
    }
    if (file?.format !== FORMAT) throw new Error('This file is not a robot configuration')
    if (file.version !== VERSION)
        throw new Error(
            `This file is configuration version ${file.version}; this app reads version ${VERSION}`
        )
    const robot = (file.robot ?? {}) as Record<string, unknown>
    const asText = (value: unknown) => (typeof value === 'string' ? value : '')
    return {
        format: FORMAT,
        version: VERSION,
        exportedAt: asText(file.exportedAt),
        robot: {
            name: asText(robot.name),
            variant: asText(robot.variant),
            deviceId: asText(robot.deviceId),
            firmwareVersion: asText(robot.firmwareVersion),
            buildTarget: asText(robot.buildTarget)
        },
        servo: ServoSettings.fromJSON(file.servo),
        peripherals: PeripheralSettings.fromJSON(file.peripherals)
    }
}

/**
 * A file restores the robot it came from, name included; on another robot it is a starting calibration, so that robot
 * keeps its name. The variant follows the file, as a calibration only means something for its own geometry.
 */
export function planImport(config: RobotConfig, robot: FeaturesDataResponse): ImportPlan {
    const plan: ImportPlan = { warnings: [] }
    const variant = knownVariant(config.robot.variant)
    if (variant && variant !== knownVariant(robot.variant)) plan.variant = variant
    if (config.robot.deviceId === robot.deviceId) {
        if (config.robot.name && config.robot.name !== robot.robotName)
            plan.rename = config.robot.name
    } else {
        plan.warnings.push(
            `This file was saved on ${config.robot.name || 'another robot'}; ${robot.robotName || 'this robot'} keeps its own name.`
        )
    }
    if (config.robot.buildTarget && config.robot.buildTarget !== robot.firmwareBuiltTarget)
        plan.warnings.push(
            `The file was saved on an ${config.robot.buildTarget} board; its pins may not fit this ${robot.firmwareBuiltTarget}.`
        )
    return plan
}

/** Applies a file in the order the robot needs: the variant before the calibration made for it. */
export async function applyConfig(config: RobotConfig, plan: ImportPlan) {
    if (plan.variant) {
        const refused = await chooseVariant(plan.variant)
        if (refused) throw new Error(refused)
    }
    await robotRequest({ servoSettings: config.servo })
    await robotRequest({ peripheralSettings: config.peripherals })
    if (plan.rename) await robotRequest({ robotNameUpdate: { name: plan.rename } })
}
