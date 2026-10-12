import { writable } from 'svelte/store'

export const mpu = writable({ heading: 0 })
export const sonar = writable([0, 0])
