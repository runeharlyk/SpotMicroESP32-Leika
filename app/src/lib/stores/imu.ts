import { writable } from 'svelte/store'
import { IMUData } from '$lib/platform_shared/message'

const imu_data: IMUData[] = []
const maxIMUData = 100

export const imu = (() => {
    const { subscribe, update } = writable(imu_data)

    return {
        subscribe,
        addData: (content: IMUData) => {
            update(imu_data => {
                return [...imu_data, content].slice(-maxIMUData)
            })
        }
    }
})()
