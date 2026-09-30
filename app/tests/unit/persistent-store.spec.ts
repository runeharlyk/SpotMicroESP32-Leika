import { describe, it, expect, beforeEach } from 'vitest'
import { get } from 'svelte/store'
import { persistentStore } from '../../src/lib/utilities/svelte-utilities'

describe('a store kept in localStorage', () => {
    beforeEach(() => localStorage.clear())

    it('comes back with what was saved', () => {
        persistentStore('robots', ['Pico']).set(['Pico', 'Yertle'])
        expect(get(persistentStore('robots', []))).toEqual(['Pico', 'Yertle'])
    })

    // A value an older app wrote, or one cut short, must not stop the app from starting.
    it('starts from its initial value when the saved one does not parse', () => {
        localStorage.setItem('robots', '["Pico", "Yer')
        const store = persistentStore('robots', ['Pico'])
        expect(get(store)).toEqual(['Pico'])
        expect(JSON.parse(localStorage.getItem('robots')!)).toEqual(['Pico'])
    })
})
