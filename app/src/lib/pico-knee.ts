/**
 * The Pico's knee four-bar (simulation/src/resources/spot_pico/linkage.py): the knee servo sits in the coxa, coaxial
 * with the femur, and turns the tibia through a crank, a pushrod and a lever on the tibia. Angles are radians in the
 * femur's plane, turning from the femur line towards its lever, which is about +x in the model's link frames (the lever
 * lobe of both tibia meshes); each is zero at the CAD stance, where the model's joints are zero.
 */
const CRANK = 2.2222
const ROD = 5.0125
const LEVER = 1.3006
const FEMUR = 5.7925
const CRANK_AT_STANCE = (65.31 * Math.PI) / 180
const LEVER_AT_STANCE = (85.68 * Math.PI) / 180

const wrap = (angle: number) => angle - 2 * Math.PI * Math.floor((angle + Math.PI) / (2 * Math.PI))

// Past either end of its travel the linkage cannot close; it then sits at the end it reached, as the real one binds.
const clampedAcos = (cosine: number) => Math.acos(Math.min(1, Math.max(-1, cosine)))

/** Of the two ways the linkage closes, the one its stance assembles: nearest the stance angle. */
const nearest = (candidates: number[], stance: number) =>
    candidates.reduce((best, angle) =>
        Math.abs(wrap(angle - stance)) < Math.abs(wrap(best - stance)) ? angle : best
    )

/** The tibia's angle to the femur for the crank's angle to the femur. */
export function tibiaFromCrank(crank: number) {
    const phi = CRANK_AT_STANCE + crank
    const dy = CRANK * Math.cos(phi) - FEMUR
    const dz = CRANK * Math.sin(phi)
    const distance = Math.hypot(dy, dz)
    const toCrank = Math.atan2(dz, dy)
    const spread = clampedAcos((distance ** 2 + LEVER ** 2 - ROD ** 2) / (2 * distance * LEVER))
    return wrap(nearest([toCrank + spread, toCrank - spread], LEVER_AT_STANCE) - LEVER_AT_STANCE)
}

/** The crank's angle to the femur that holds the tibia at this angle to it. */
export function crankFromTibia(tibia: number) {
    const psi = LEVER_AT_STANCE + tibia
    const y = FEMUR + LEVER * Math.cos(psi)
    const z = LEVER * Math.sin(psi)
    const distance = Math.hypot(y, z)
    const toLever = Math.atan2(z, y)
    const spread = clampedAcos((distance ** 2 + CRANK ** 2 - ROD ** 2) / (2 * distance * CRANK))
    return wrap(nearest([toLever + spread, toLever - spread], CRANK_AT_STANCE) - CRANK_AT_STANCE)
}
