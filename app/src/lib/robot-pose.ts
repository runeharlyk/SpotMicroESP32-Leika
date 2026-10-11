import type { Readable } from 'svelte/store'
import { TelemetryBatch } from '$lib/platform_shared/message'
import { jointTargets } from '$lib/simulation/controllers'
import { JOINT_MAPS } from '$lib/simulation/robots'
import { kinConfig, type Variant } from '$lib/simulation/firmware/kin-config'
import { BodyState, DEG2RAD_F, inverseKinematics } from '$lib/simulation/firmware/kinematics'
import { DIR } from '$lib/simulation/firmware/motion'

/** A model's joint values (rad), by joint name. */
export type JointValues = Record<string, number>

/**
 * The model's joints for the servo angles a tick reports: radians in servo space, which the firmware converts from its
 * degrees with DEG_TO_RAD_F, so dividing by the same constant gives its degrees back exactly.
 */
export const tickJointValues = (variant: Variant, angles: number[]): JointValues =>
    jointTargets(
        JOINT_MAPS[variant],
        angles.map(angle => angle / DEG2RAD_F)
    )

/** The model's joints in the stance the firmware's body starts in, before any mode moves it. */
export function stanceJointValues(variant: Variant): JointValues {
    const cfg = kinConfig(variant)
    return jointTargets(
        JOINT_MAPS[variant],
        inverseKinematics(cfg, new BodyState(cfg)).map((angle, i) => angle * DIR[i])
    )
}

/** The robot link: whether it is open, and its telemetry stream. */
export interface TelemetryLink extends Readable<boolean> {
    on(type: typeof TelemetryBatch, listener: (batch: TelemetryBatch) => void): () => void
}

/**
 * Hands `show` the joints the robot reports, from the last tick of each telemetry batch (10 Hz), and the stance
 * whenever no robot is connected. Subscribing makes the robot record every control tick and stream it, about 30 kB/s,
 * so the returned function, which drops the subscription, belongs in the view's teardown.
 */
export function followRobotPose(
    link: TelemetryLink,
    variant: Variant,
    show: (joints: JointValues) => void
) {
    const stance = stanceJointValues(variant)
    show(stance)
    const stopBatches = link.on(TelemetryBatch, batch => {
        const latest = batch.ticks.at(-1)
        if (latest?.angles.length === 12) show(tickJointValues(variant, latest.angles))
    })
    const stopLink = link.subscribe(open => {
        if (!open) show(stance)
    })
    return () => {
        stopBatches()
        stopLink()
    }
}
