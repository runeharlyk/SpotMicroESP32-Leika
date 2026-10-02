import type { Variant } from '$lib/kinematics-variants'
import type { GaitCoef } from './pico-gait'
import { FirmwareController, TrainingController, type JointMap } from './controllers'
import type { SimController } from './robot-sim'

export type RobotId = 'pico' | 'spot_micro' | 'yertle'

export interface ControllerDefinition {
    id: 'firmware' | 'training'
    label: string
    create(assets: { gaitCoef: GaitCoef }): SimController
}

export interface RobotDefinition {
    id: RobotId
    label: string
    /** The firmware variant the robot runs, which also picks its 3D model. */
    variant: Variant
    /** Static files the app publishes for the simulation (scripts/build_sim_models.js). */
    sceneFile: string
    meshZip: string | null
    /** Its forward direction on the floor plane of its scene. */
    forward: [number, number]
    controllers: ControllerDefinition[]
}

const legs = (prefixes: string[], joints: string[]) =>
    prefixes.flatMap(leg => joints.map(joint => `${leg}${joint}`))

// Checked by posing each scene with the firmware's stand angles: the model's feet land where the
// firmware's foot targets are. The generated scenes keep the URDF joint names.
const SPOT_MICRO_JOINTS: JointMap = {
    joints: legs(
        ['front_left', 'front_right', 'rear_left', 'rear_right'],
        ['_shoulder', '_leg', '_foot']
    ),
    sign: new Array(12).fill(1),
    offset: new Array(12).fill(0)
}
const YERTLE_JOINTS: JointMap = {
    joints: legs(['lf', 'rf', 'lb', 'rb'], ['_shoulder', '_thigh', '_shin']),
    sign: new Array(12).fill(1),
    offset: new Array(12).fill(0),
    kneeRelativeToBody: true
}

// Fitted by simulation/fit_pico_joint_map.py: the MJCF feet follow the firmware's MINI foot targets
// to about 1.3 mm, with the two body origins up to 13 mm apart.
const degrees = (values: number[]) => values.map(value => (value * Math.PI) / 180)
const PICO_JOINTS: JointMap = {
    joints: legs(['fl', 'fr', 'rl', 'rr'], ['_hip_joint', '_femur_joint', '_tibia_joint']),
    sign: [-1, -1, 1, 1, -1, -1, -1, -1, 1, 1, -1, -1],
    offset: degrees([7.3, 54.6, 101.2, 6.0, 52.9, -99.4, 5.8, 54.4, 100.4, 6.7, 50.6, -98.1])
}

export const ROBOTS: RobotDefinition[] = [
    {
        id: 'pico',
        label: 'Pico',
        variant: 'SPOTMICRO_ESP32_MINI',
        sceneFile: 'spot_pico_scene.xml',
        meshZip: 'spot_pico.zip',
        forward: [0, -1],
        controllers: [
            {
                id: 'firmware',
                label: 'Firmware (MINI)',
                create: () => new FirmwareController('SPOTMICRO_ESP32_MINI', PICO_JOINTS)
            },
            {
                id: 'training',
                label: 'Training (Python)',
                create: ({ gaitCoef }) => new TrainingController(gaitCoef)
            }
        ]
    },
    {
        id: 'spot_micro',
        label: 'Spot Micro',
        variant: 'SPOTMICRO_ESP32',
        sceneFile: 'sim_spot_micro.xml',
        meshZip: null,
        forward: [1, 0],
        controllers: [
            {
                id: 'firmware',
                label: 'Firmware',
                create: () => new FirmwareController('SPOTMICRO_ESP32', SPOT_MICRO_JOINTS)
            }
        ]
    },
    {
        id: 'yertle',
        label: 'Yertle',
        variant: 'SPOTMICRO_YERTLE',
        sceneFile: 'sim_yertle.xml',
        meshZip: 'sim_yertle_meshes.zip',
        forward: [1, 0],
        controllers: [
            {
                id: 'firmware',
                label: 'Firmware',
                create: () => new FirmwareController('SPOTMICRO_YERTLE', YERTLE_JOINTS)
            }
        ]
    }
]

export const robotById = (id: string) => ROBOTS.find(robot => robot.id === id)

export interface SimChoice {
    robot: RobotId
    controller: ControllerDefinition['id']
}

/**
 * The saved choice where it names a robot and controller that exist; otherwise the robot the
 * last connected robot reported, or the Pico, with its first controller.
 */
export function resolveChoice(saved: Partial<SimChoice> | null, reported: Variant | undefined) {
    const robot =
        (saved?.robot && robotById(saved.robot)) ||
        ROBOTS.find(candidate => candidate.variant === reported) ||
        ROBOTS[0]
    const controller =
        robot.controllers.find(candidate => candidate.id === saved?.controller) ??
        robot.controllers[0]
    return { robot, controller }
}
