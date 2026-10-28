import {
    ControllerData,
    KinematicData,
    ModeData,
    ModesEnum,
    WalkGaitData,
    WalkGaits
} from '$lib/platform_shared/message'
import { writable, type Writable } from 'svelte/store'

export const model = writable()

export const mode: Writable<ModeData> = writable(ModeData.create({ mode: ModesEnum.DEACTIVATED }))

export const walkGait: Writable<WalkGaitData> = writable(
    WalkGaitData.create({ gait: WalkGaits.TROT })
)

export const kinematicData = writable(KinematicData.create())

/** Centred sticks with the sliders where the app starts them. */
export const startingInput = () =>
    ControllerData.create({
        left: { x: 0, y: 0 },
        right: { x: 0, y: 0 },
        height: 0.7,
        s1: 0.5,
        speed: 0.5
    })

/** The controls as they stand: the sticks and sliders shown, for the robot they drive (stores/drive.ts). */
export const input: Writable<ControllerData> = writable(startingInput())

/** The sticks and sliders the robot is sent. */
export const outControllerData: Writable<ControllerData> = writable(startingInput())

function enumToValuesAndLabels<T extends number>(enumObj: Record<string, T | string>) {
    const entries = Object.entries(enumObj).filter(
        ([key, v]) => typeof v === 'number' && key !== 'UNRECOGNIZED'
    ) as [string, T][]

    return {
        values: entries.map(([, v]) => v),
        labels: Object.fromEntries(
            entries.map(([k, v]) => [v, k.charAt(0) + k.slice(1).toLowerCase()])
        ) as Record<T, string>
    }
}

const modesData = enumToValuesAndLabels<ModesEnum>(ModesEnum)
// A clip puts the robot in ANIMATE and takes it out again; it is not a mode to pick.
export const modes = modesData.values.filter(mode => mode !== ModesEnum.ANIMATE)
export const modeLabels: Record<ModesEnum, string> = {
    ...modesData.labels,
    [ModesEnum.WALK_NN]: 'Learned walk'
}

/** The modes to offer: WALK_NN only on a robot that reports a policy which passed its self-check. */
export const selectableModes = (walkNn: boolean) =>
    modes.filter(mode => mode !== ModesEnum.WALK_NN || walkNn)

const walkGaitsData = enumToValuesAndLabels<WalkGaits>(WalkGaits)
export const walkGaits = walkGaitsData.values
export const walkGaitLabels = walkGaitsData.labels
