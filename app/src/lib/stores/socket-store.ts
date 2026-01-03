import { AnglesData } from '$lib/platform_shared/message'
import { writable, type Writable } from 'svelte/store'

export const servoAnglesOut: Writable<AnglesData> = writable(
    AnglesData.create({ angles: [0, 45, -90, 0, 45, -90, 0, 45, -90, 0, 45, -90] })
)
export const servoAngles: Writable<AnglesData> = writable(
    AnglesData.create({ angles: [0, 45, -90, 0, 45, -90, 0, 45, -90, 0, 45, -90] })
)

export const logs = writable([] as string[])
export const mpu = writable({ heading: 0 })
export const sonar = writable([0, 0])
export const distances = writable({})

export interface socketDataCollection {
    angles: Writable<AnglesData>
    logs: Writable<string[]>
    mpu: Writable<unknown>
    distances: Writable<unknown>
}

export const socketData = {
    angles: servoAngles,
    logs,
    mpu,
    distances
}
