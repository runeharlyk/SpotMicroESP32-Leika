import { describe, it, expect } from 'vitest'
import { get } from 'svelte/store'
import { createHistoryStore } from '$lib/stores/history-store'
import { IMUData } from '$lib/platform_shared/message'

describe('createHistoryStore', () => {
    it('starts empty', () => {
        const store = createHistoryStore(IMUData, 10)
        expect(get(store)).toEqual([])
    })

    it('appends data in order', () => {
        const store = createHistoryStore(IMUData, 10)
        store.addData(IMUData.create({ x: 1 }))
        store.addData(IMUData.create({ x: 2 }))
        expect(get(store).map(d => d.x)).toEqual([1, 2])
    })

    it('keeps only the most recent maxItems entries', () => {
        const store = createHistoryStore(IMUData, 3)
        for (let i = 0; i < 5; i++) store.addData(IMUData.create({ x: i }))
        const data = get(store)
        expect(data).toHaveLength(3)
        expect(data.map(d => d.x)).toEqual([2, 3, 4])
    })
})
