import { ModesEnum } from '$lib/platform_shared/message'

type ButtonEdge = { justPressed: boolean } | undefined

// Standard gamepad layout: 0-3 are the face buttons, 12 and 13 the d-pad up and down.
// Ordered safest first, so deactivate wins when several are pressed at once.
const MODE_BUTTONS: [number, ModesEnum][] = [
    [3, ModesEnum.DEACTIVATED],
    [2, ModesEnum.REST],
    [1, ModesEnum.STAND],
    [0, ModesEnum.WALK]
]
const HEIGHT_STEP = 0.1

export function gamepadCommand(buttons: ButtonEdge[]) {
    const mode = MODE_BUTTONS.find(([index]) => buttons[index]?.justPressed)?.[1]
    const heightStep =
        buttons[12]?.justPressed ? HEIGHT_STEP
        : buttons[13]?.justPressed ? -HEIGHT_STEP
        : 0
    return { mode, heightStep }
}

export const stepHeight = (height: number, step: number) => Math.min(Math.max(height + step, 0), 1)
