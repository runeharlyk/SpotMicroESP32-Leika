import { writable } from 'svelte/store'
import type { IMUMsg } from '$lib/types/models'

const maxIMUData = 100

export const imu = (() => {
    const { subscribe, update } = writable({
        x: [] as number[],
        y: [] as number[],
        z: [] as number[],
        heading: [] as number[],
        altitude: [] as number[],
        pressure: [] as number[],
        bmp_temp: [] as number[]
    })

    const addData = (content: IMUMsg) => {
        update(data => {
            if (content.imu && content.imu[4]) {
                data.x = [...data.x, content.imu[0]].slice(-maxIMUData)
                data.y = [...data.y, content.imu[1]].slice(-maxIMUData)
                data.z = [...data.z, content.imu[2]].slice(-maxIMUData)
            }

            if (content.mag && content.mag[4]) {
                data.heading = [...data.heading, content.mag[3]].slice(-maxIMUData)
            }

            if (content.bmp && content.bmp[3]) {
                data.pressure = [...data.pressure, content.bmp[0]].slice(-maxIMUData)
                data.altitude = [...data.altitude, content.bmp[1]].slice(-maxIMUData)
                data.bmp_temp = [...data.bmp_temp, content.bmp[2]].slice(-maxIMUData)
            }

            return data
        })
    }

    return { subscribe, addData }
})()
