import { writable } from 'svelte/store'
import { browser } from '$app/environment'

// A saved value that does not parse, written by an older app or cut short, gives way to the initial one.
const restore = <T>(key: string, initialValue: T): T => {
    const savedValue = browser ? localStorage.getItem(key) : null
    if (savedValue === null) return initialValue
    try {
        return JSON.parse(savedValue)
    } catch {
        return initialValue
    }
}

export const persistentStore = <T>(key: string, initialValue: T) => {
    const data = restore(key, initialValue)
    const store = writable<T>()

    store.subscribe(value => {
        if (browser) localStorage.setItem(key, JSON.stringify(value))
    })

    store.set(data)

    return store
}
