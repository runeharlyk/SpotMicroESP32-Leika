import { VARIANT_DIMENSIONS, type Variant } from '$lib/kinematics-variants'

export type { Variant }

/** KinConfig of esp32/include/kinematics.h for one variant: dimensions and the limits derived from them. */
export interface KinConfig {
    variant: Variant
    coxa: number
    coxa_offset: number
    femur: number
    tibia: number
    L: number
    W: number
    mountOffsets: number[][]
    defaultFeet: number[][]
    maxRoll: number
    maxPitch: number
    maxBodyShiftX: number
    maxBodyShiftZ: number
    minBodyHeight: number
    bodyHeightRange: number
    maxStepLength: number
    maxStepHeight: number
    defaultStepDepth: number
    defaultBodyHeight: number
    defaultStepHeight: number
}

export function kinConfig(variant: Variant): KinConfig {
    const { coxa, coxa_offset, femur, tibia, L, W } = VARIANT_DIMENSIONS[variant]
    const mountOffsets = [
        [L / 2, 0, W / 2],
        [L / 2, 0, -W / 2],
        [-L / 2, 0, W / 2],
        [-L / 2, 0, -W / 2]
    ]
    const maxLegReach = femur + tibia - coxa_offset
    const minBodyHeight = maxLegReach * 0.45
    const bodyHeightRange = maxLegReach * 0.9 - minBodyHeight
    const defaultBodyHeight = minBodyHeight + bodyHeightRange / 2
    return {
        variant,
        coxa,
        coxa_offset,
        femur,
        tibia,
        L,
        W,
        mountOffsets,
        defaultFeet: mountOffsets.map(([x, , z], i) => [x, 0, i % 2 === 0 ? z + coxa : z - coxa]),
        maxRoll: 20,
        maxPitch: 15,
        maxBodyShiftX: W / 3,
        maxBodyShiftZ: W / 3,
        minBodyHeight,
        bodyHeightRange,
        maxStepLength: maxLegReach * 0.8,
        maxStepHeight: maxLegReach / 2,
        defaultStepDepth: 0.002,
        defaultBodyHeight,
        defaultStepHeight: defaultBodyHeight / 2
    }
}
