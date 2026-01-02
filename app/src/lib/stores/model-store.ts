import { HumanInputData, KinematicData, ModeData, ModesEnum, WalkGaitData, WalkGaits } from '$lib/platform_shared/websocket_message'
import { persistentStore } from '$lib/utilities/svelte-utilities'
import { writable, type Writable } from 'svelte/store'

export const emulateModel = writable(true)

export const jointNames = persistentStore('joint_names', <string[]>[])

export const model = writable()

export const mode: Writable<ModeData> = writable(ModeData.create({ mode: ModesEnum.DEACTIVATED }))

export const walkGait: Writable<WalkGaitData> = writable( WalkGaitData.create({gait: WalkGaits.TROT }) )

export const outControllerData = writable( HumanInputData.create( {left: {x:0,y:0}, right: {x:0,y:0}, height:0, s1:0, speed:0} ) )

export const kinematicData = writable(KinematicData.create())

export const input: Writable<HumanInputData> = writable( HumanInputData.create( {left: {x:0,y:0}, right: {x:0,y:0}, height:0, s1:0, speed:0} ) )


// Following code is generated from CLAUDE CODE
// Auto-generate modes array from ModesEnum (excluding UNRECOGNIZED)
export const modes = Object.values(ModesEnum)
    .filter((v): v is ModesEnum => typeof v === 'number' && v !== ModesEnum.UNRECOGNIZED)

// Auto-generate mode labels from enum keys
export const modeLabels: Record<ModesEnum, string> = Object.entries(ModesEnum)
    .filter(([_, v]) => typeof v === 'number' && v !== ModesEnum.UNRECOGNIZED)
    .reduce((acc, [key, value]) => {
        acc[value as ModesEnum] = key.charAt(0) + key.slice(1).toLowerCase()
        return acc
    }, {} as Record<ModesEnum, string>)

// Auto-generate walk gaits array from WalkGaits enum (excluding UNRECOGNIZED)
export const walkGaits = Object.values(WalkGaits)
    .filter((v): v is WalkGaits => typeof v === 'number' && v !== WalkGaits.UNRECOGNIZED)

// Auto-generate walk gait labels from enum keys
export const walkGaitLabels: Record<WalkGaits, string> = Object.entries(WalkGaits)
    .filter(([_, v]) => typeof v === 'number' && v !== WalkGaits.UNRECOGNIZED)
    .reduce((acc, [key, value]) => {
        acc[value as WalkGaits] = key.charAt(0) + key.slice(1).toLowerCase()
        return acc
    }, {} as Record<WalkGaits, string>)
