import { PUBLIC_EMBEDDED_BUILD } from '$env/static/public'

/** Set by `build:embedded`: the app the robot serves from its own flash. */
export const EMBEDDED_BUILD = PUBLIC_EMBEDDED_BUILD === 'true'

// Flash is scarce, so the robot-served app leaves out the 3D view with three.js and the models.
export const HAS_3D_VIEW = !EMBEDDED_BUILD
