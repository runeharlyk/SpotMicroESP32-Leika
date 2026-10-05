import { readable, derived } from 'svelte/store'
import { shapeStick } from '$lib/utilities/stick'

export type GamepadState = {
    available: boolean
    gamepads: Gamepad[]
}

let raf = 0
let running = false

export const gamepads = readable<GamepadState>({ available: false, gamepads: [] }, set => {
    const update = () => {
        const pads = navigator.getGamepads?.() ?? []
        const list = Array.from(pads)
            .map(p => p || null)
            .filter(Boolean) as Gamepad[]
        set({ available: 'getGamepads' in navigator, gamepads: list })
        raf = requestAnimationFrame(update)
    }

    const onConnect = () => update()
    const onDisconnect = () => update()
    const onVis = () => {
        if (document.hidden) {
            running = false
            cancelAnimationFrame(raf)
        } else if (!running) {
            running = true
            raf = requestAnimationFrame(update)
        }
    }

    window.addEventListener('gamepadconnected', onConnect)
    window.addEventListener('gamepaddisconnected', onDisconnect)
    document.addEventListener('visibilitychange', onVis)

    running = true
    raf = requestAnimationFrame(update)

    return () => {
        running = false
        cancelAnimationFrame(raf)
        window.removeEventListener('gamepadconnected', onConnect)
        window.removeEventListener('gamepaddisconnected', onDisconnect)
        document.removeEventListener('visibilitychange', onVis)
    }
})

export const gamepad = derived(gamepads, s =>
    s.available && s.gamepads.length ? s.gamepads[0] : null
)

export const hasGamepad = derived(gamepads, s => s.available && s.gamepads.length > 0)

// Each stick is shaped as a whole: a dead zone per axis let a forward push's sideways drift through.
const stick = (axes: readonly number[], first: number) => {
    const shaped = shapeStick({ x: axes[first] ?? 0, y: axes[first + 1] ?? 0 })
    return [shaped.x, shaped.y]
}

export const gamepadAxes = derived(gamepad, g =>
    g ? [...stick(g.axes, 0), ...stick(g.axes, 2)] : [0, 0, 0, 0]
)

type ButtonEdge = { pressed: boolean; value: number; justPressed: boolean; justReleased: boolean }
const prev = new Map<number, { pressed: boolean; value: number }[]>()

export const gamepadButtonsEdges = derived(gamepad, g => {
    if (!g) return [] as ButtonEdge[]
    const p = prev.get(g.index) || []
    const out = g.buttons.map((b, i): ButtonEdge => {
        const pr = p[i] || { pressed: false, value: 0 }
        const pressed = !!b.pressed || b.value > 0.5
        return {
            pressed,
            value: b.value,
            justPressed: pressed && !pr.pressed,
            justReleased: !pressed && pr.pressed
        }
    })
    prev.set(
        g.index,
        out.map(x => ({ pressed: x.pressed, value: x.value }))
    )
    return out
})
