/**
 * Leg dimensions (m) of each kinematics variant, as KinConfig in esp32/include/kinematics.h.
 * tests/unit/variant-kinematics.spec.ts fails if they drift from the firmware.
 */
export const VARIANT_DIMENSIONS = {
    SPOTMICRO_ESP32: {
        coxa: 0.0605,
        coxa_offset: 0.01,
        femur: 0.1112,
        tibia: 0.1185,
        L: 0.2075,
        W: 0.078
    },
    SPOTMICRO_ESP32_MINI: {
        coxa: 0.035,
        coxa_offset: 0.0,
        femur: 0.06,
        tibia: 0.06,
        L: 0.16,
        W: 0.08
    },
    SPOTMICRO_YERTLE: { coxa: 0.035, coxa_offset: 0.0, femur: 0.13, tibia: 0.13, L: 0.24, W: 0.078 }
}

export type Variant = keyof typeof VARIANT_DIMENSIONS

/** The variant a robot reported, when it is one this app knows; a reported name may carry a version suffix like _V2. */
export const knownVariant = (reported: string | undefined): Variant | undefined => {
    const name = reported?.replace(/_V\d+$/, '')
    return name && name in VARIANT_DIMENSIONS ? (name as Variant) : undefined
}
