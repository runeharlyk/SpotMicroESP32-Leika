import { AnalyticsData } from '$lib/platform_shared/websocket_message'
import { writable } from 'svelte/store'

const analytics_data: AnalyticsData[] = []

const maxAnalyticsData = 100

function createAnalytics() {
    const { subscribe, update } = writable(analytics_data)

    return {
        subscribe,
        addData: (content: AnalyticsData) => {
            update(analytics_data => {
                return [...analytics_data, content].slice(-maxAnalyticsData)
            })
        }
    }
}

export const analytics = createAnalytics()
