import { writable } from 'svelte/store'
import { IMUData } from '$lib/platform_shared/message'
import { socket } from './socket'

const maxIMUData = 100

export const imu = (() => {
    const { subscribe, update } = writable<IMUData[]>([])

    let unsubscribe: (() => void) | null = null
    let listenerCount = 0

    const addData = (content: IMUData) => {
        update(data => [...data, content].slice(-maxIMUData))
    }

    return {
        subscribe,
        addData,
        listen: () => {
            listenerCount++
            if (!unsubscribe) {
                unsubscribe = socket.on(IMUData, addData)
            }
        },
        stop: () => {
            listenerCount = Math.max(0, listenerCount - 1)
            if (listenerCount === 0 && unsubscribe) {
                unsubscribe()
                unsubscribe = null
            }
        }
    }
})()
