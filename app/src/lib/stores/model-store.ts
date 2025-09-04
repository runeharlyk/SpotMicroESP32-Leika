import type { ControllerInput } from '$lib/types/models'
import { persistentStore } from '$lib/utilities/svelte-utilities'
import { writable, type Writable } from 'svelte/store'

export const emulateModel = writable(true)

export const jointNames = persistentStore('joint_names', <string[]>[])

export const model = writable()

export const modes = [
  'deactivated',
  'idle',
  'calibration',
  'rest',
  'stand',
  'walk',
  'animate'
] as const

export type Modes = (typeof modes)[number]

export enum ModesEnum {
  Deactivated = 0,
  Idle = 1,
  Calibration = 2,
  Rest = 3,
  Stand = 4,
  Walk = 5,
  Animate = 6
}

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

export const mode: Writable<ModesEnum> = writable(ModesEnum.Deactivated)

export const walkGait: Writable<WalkGaits> = writable(WalkGaits.Trot)

export const outControllerData = writable([0, 0, 0, 0, 0, 1, 0])

export const kinematicData = writable([0, 0, 0, 0, 1, 0])

export const input: Writable<ControllerInput> = writable({
    left: { x: 0, y: 0 },
    right: { x: 0, y: 0 },
    height: 0.5,
    speed: 0.5,
    s1: 0.05
})
