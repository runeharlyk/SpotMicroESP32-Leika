import { AnalyticsData } from '$lib/platform_shared/message'
import { writable } from 'svelte/store'
import { socket } from './socket'

const maxAnalyticsData = 100

function createAnalytics() {
    const { subscribe, update } = writable<AnalyticsData[]>([])

    let unsubscribe: (() => void) | null = null
    let listenerCount = 0

    const addData = (content: AnalyticsData) => {
        update(data => [...data, content].slice(-maxAnalyticsData))
    }

    return {
        subscribe,
        addData,
        listen: () => {
            listenerCount++
            if (!unsubscribe) {
                unsubscribe = socket.on(AnalyticsData, addData)
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
}

export const analytics = createAnalytics()
