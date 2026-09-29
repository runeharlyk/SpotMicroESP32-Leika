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
    /** Radius of its feet (m), so it can be spawned resting on the floor. */
    footRadius: number
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

export const ROBOTS: RobotDefinition[] = [
    {
        id: 'pico',
        label: 'Pico',
        variant: 'SPOTMICRO_ESP32_MINI',
        sceneFile: 'spot_pico_scene.xml',
        meshZip: 'spot_pico.zip',
        footRadius: 0.009,
        forward: [0, -1],
        controllers: [
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
        footRadius: 0.02,
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
        footRadius: 0.01,
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
