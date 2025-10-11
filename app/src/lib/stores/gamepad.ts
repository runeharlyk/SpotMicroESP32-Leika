import { readable, derived } from 'svelte/store'

export type GamepadState = {
    available: boolean
    gamepads: Gamepad[]
}

export const gamepads = readable<GamepadState>({ available: false, gamepads: [] }, set => {
    const update = () => {
        const hasGamepadAPI = 'getGamepads' in navigator
        if (!hasGamepadAPI) {
            set({ available: false, gamepads: [] })
            return
        }

        const gps = navigator.getGamepads?.() ?? []
        const validGamepads = gps.filter(Boolean) as Gamepad[]
        set({
            available: true,
            gamepads: validGamepads
        })
        raf = requestAnimationFrame(update)
    }

    window.addEventListener('gamepadconnected', update)
    window.addEventListener('gamepaddisconnected', update)
    let raf = requestAnimationFrame(update)

    return () => {
        cancelAnimationFrame(raf)
        window.removeEventListener('gamepadconnected', update)
        window.removeEventListener('gamepaddisconnected', update)
    }
})

export const gamepad = derived(gamepads, $gamepads =>
    $gamepads.available && $gamepads.gamepads.length > 0 ? $gamepads.gamepads[0] : null
)

export const gamepadAxes = derived(gamepad, $gamepad => $gamepad?.axes ?? [0, 0, 0, 0])

export const gamepadButtons = derived(gamepad, $gamepad => $gamepad?.buttons ?? [])

export const hasGamepad = derived(
    gamepads,
    $gamepads => $gamepads.available && $gamepads.gamepads.length > 0
)
