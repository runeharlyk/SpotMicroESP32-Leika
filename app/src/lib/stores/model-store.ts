import { HumanInputData, ModeData, ModesEnum } from '$lib/platform_shared/websocket_message'
import { persistentStore } from '$lib/utilities/svelte-utilities'
import { writable, type Writable } from 'svelte/store'

export const emulateModel = writable(true)

export const jointNames = persistentStore('joint_names', <string[]>[])

export const model = writable()

export const modes = ['deactivated', 'idle', 'calibration', 'rest', 'stand', 'walk'] as const

export type Modes = (typeof modes)[number]



export enum WalkGaits {
    Trot = 0,
    Crawl = 1
}

export const walkGaits = ['trot', 'crawl'] as const

export const walkGaitLabels: Record<WalkGaits, string> = {
    [WalkGaits.Trot]: 'Trot',
    [WalkGaits.Crawl]: 'Crawl'
}

export const walkGaitToMode = (gait: WalkGaits): 'trot' | 'crawl' => {
    return gait === WalkGaits.Trot ? 'trot' : 'crawl'
}

export const mode: Writable<ModeData> = writable(ModeData.create({ mode: ModesEnum.DEACTIVATED }))

export const walkGait: Writable<WalkGaits> = writable(WalkGaits.Trot)

export const outControllerData = writable( HumanInputData.create( {left: {x:0,y:0}, right: {x:0,y:0}, height:0, s1:0, speed:0} ) )

export const kinematicData = writable([0, 0, 0, 0, 1, 0])

export const input: Writable<HumanInputData> = writable( HumanInputData.create( {left: {x:0,y:0}, right: {x:0,y:0}, height:0, s1:0, speed:0} ) )
