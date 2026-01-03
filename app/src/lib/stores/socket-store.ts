import { AnglesData } from '$lib/platform_shared/message'
import { writable, type Writable } from 'svelte/store'

export const servoAnglesOut: Writable<AnglesData> = writable(
    AnglesData.create({ angles: [0, 45, -90, 0, 45, -90, 0, 45, -90, 0, 45, -90] })
)
export const servoAngles: Writable<AnglesData> = writable(
    AnglesData.create({ angles: [0, 45, -90, 0, 45, -90, 0, 45, -90, 0, 45, -90] })
)

export const mpu = writable({ heading: 0 })
export const sonar = writable([0, 0])
