import type { ControllerData } from '$lib/platform_shared/message'

/**
 * The same input with both sticks centred: what the robot is sent whenever the link drops or the tab
 * is backgrounded. Pose and speed stay, so it holds its stance rather than collapsing.
 */
export const stopped = (data: ControllerData): ControllerData => ({
    ...data,
    left: { x: 0, y: 0 },
    right: { x: 0, y: 0 }
})

/** Well inside the firmware's 500 ms link timeout (esp32/include/motion_inbox.h). */
export const KEEPALIVE_MS = 200

const steering = ({ left, right }: ControllerData) =>
    [left?.x, left?.y, right?.x, right?.y].some(Boolean)

/**
 * Re-sends the controller input while a stick is off centre. The app otherwise sends input only when
 * it changes, and the robot stops walking when it hears nothing for 500 ms, so a stick held still
 * would look like a lost controller. Returns a function that stops the re-sending.
 */
export function keepControlAlive(
    current: () => ControllerData,
    send: (data: ControllerData) => void,
    periodMs = KEEPALIVE_MS
): () => void {
    const id = setInterval(() => {
        const data = current()
        if (steering(data)) send(data)
    }, periodMs)
    return () => clearInterval(id)
}
