import { writable } from 'svelte/store'

export const isFullscreen = writable(false)

export function toggleFullscreen() {
    isFullscreen.update(state => {
        if (!state) document.documentElement.requestFullscreen()
        else document.exitFullscreen()
        return !state
    })
}

export function exitFullscreen() {
    if (document.fullscreenElement) {
        document.exitFullscreen()
        isFullscreen.set(false)
    }
}
