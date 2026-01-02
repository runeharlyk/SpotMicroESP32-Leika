import Kinematic from '$lib/kinematic'
import {
    HumanInputData,
    KinematicData,
    ModeData,
    ModesEnum,
    WalkGaitData,
    WalkGaits
} from '$lib/platform_shared/websocket_message'
import { persistentStore } from '$lib/utilities/svelte-utilities'
import { writable, type Writable } from 'svelte/store'

export const emulateModel = writable(true)

export const jointNames = persistentStore('joint_names', <string[]>[])

export const model = writable()

export const mode: Writable<ModeData> = writable(ModeData.create({ mode: ModesEnum.DEACTIVATED }))

export const walkGait: Writable<WalkGaitData> = writable(
    WalkGaitData.create({ gait: WalkGaits.TROT })
)

export const kinematicData = writable(KinematicData.create())

export const input: Writable<HumanInputData> = writable(
    HumanInputData.create({
        left: { x: 0, y: 0 },
        right: { x: 0, y: 0 },
        height: 0.7,
        s1: 0.5,
        speed: 0.5
    })
)

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
export const modes = modesData.values
export const modeLabels = modesData.labels

const walkGaitsData = enumToValuesAndLabels<WalkGaits>(WalkGaits)
export const walkGaits = walkGaitsData.values
export const walkGaitLabels = walkGaitsData.labels
