import { readable, derived } from 'svelte/store'

export type GamepadState = {
    available: boolean
    gamepads: Gamepad[]
}

const DEADZONE = 0.15
const dz = (x: number) => {
    const a = Math.abs(x)
    if (a < DEADZONE) return 0
    return ((a - DEADZONE) / (1 - DEADZONE)) * Math.sign(x)
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

export const gamepadAxes = derived(gamepad, g => (g ? g.axes.map(dz) : [0, 0, 0, 0]))

type ButtonEdge = { pressed: boolean; value: number; justPressed: boolean; justReleased: boolean }
const prev = new Map<number, { pressed: boolean; value: number }[]>()

export const gamepadButtons = derived(gamepad, g => g?.buttons ?? [])

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
