import { writable } from 'svelte/store'
import { type MessageFns } from '$lib/platform_shared/message'
import { socket } from './socket'

// Shared factory for telemetry history stores: keeps a bounded rolling window of the most recent
// messages of a given type and ref-counts socket subscriptions so the server is only streamed to
// while something is listening. (Previously duplicated verbatim in analytics.ts and imu.ts.)
export function createHistoryStore<T>(messageType: MessageFns<T>, maxItems = 100) {
    const { subscribe, update } = writable<T[]>([])

    let unsubscribe: (() => void) | null = null
    let listenerCount = 0

    const addData = (content: T) => {
        update(data => [...data, content].slice(-maxItems))
    }

    return {
        subscribe,
        addData,
        listen: () => {
            listenerCount++
            if (!unsubscribe) {
                unsubscribe = socket.on(messageType, addData)
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
