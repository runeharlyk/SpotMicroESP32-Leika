export interface Stick {
    x: number
    y: number
}

const DEAD_ZONE = 0.15
// Within SNAP degrees of an axis the stick points along it; up to SNAP_END the angle opens out again, so turning the
// stick never makes the direction jump.
const SNAP = 10
const SNAP_END = 20
const AXES: Stick[] = [
    { x: 1, y: 0 },
    { x: 0, y: 1 },
    { x: -1, y: 0 },
    { x: 0, y: -1 }
]

/**
 * Shapes a stick for driving: a round dead zone, so drift is ignored in every direction alike, and a snap to the
 * axes, so a stick pushed mostly forward walks straight ahead instead of crabbing.
 */
export function shapeStick({ x, y }: Stick): Stick {
    const radius = Math.min(1, Math.hypot(x, y))
    if (radius < DEAD_ZONE) return { x: 0, y: 0 }
    const length = (radius - DEAD_ZONE) / (1 - DEAD_ZONE)
    const degrees = (Math.atan2(y, x) * 180) / Math.PI
    const axis = Math.round(degrees / 90)
    const off = degrees - axis * 90
    const away = Math.abs(off)
    if (away <= SNAP) {
        const along = AXES[(axis + 4) % 4]
        return { x: length * along.x, y: length * along.y }
    }
    const opened = away < SNAP_END ? ((away - SNAP) * SNAP_END) / (SNAP_END - SNAP) : away
    const shaped = ((axis * 90 + Math.sign(off) * opened) * Math.PI) / 180
    return { x: length * Math.cos(shaped), y: length * Math.sin(shaped) }
}

// The standard gamepad mapping reports a stick pushed up as -1; the on-screen sticks and the firmware read up as +1.
const gamepadStick = (axes: readonly number[], first: number) =>
    shapeStick({ x: axes[first] ?? 0, y: -(axes[first + 1] ?? 0) })

/** A gamepad's two sticks, the standard mapping's axes 0 and 1 and axes 2 and 3, shaped as the on-screen ones. */
export const gamepadSticks = (axes: readonly number[]): { left: Stick; right: Stick } => ({
    left: gamepadStick(axes, 0),
    right: gamepadStick(axes, 2)
})
