import { derived } from 'svelte/store'
import { AnimationStatus, ModeData, WalkGaitData } from '$lib/platform_shared/message'
import { WifiStatus } from '$lib/platform_shared/api'
import { socket } from '$lib/stores/socket'
import { watch, type Follower } from './watch'

/**
 * The robot's state topics the pages follow, as watches, and what the pages show of them. The mode, the gait and the
 * WiFi status keep their last value while the link is down, marked stale; the clip the robot plays is cleared then.
 */
export const robotState = (from: Follower = socket) => {
    const animationState = watch(AnimationStatus, from)
    return {
        modeState: watch(ModeData, from),
        gaitState: watch(WalkGaitData, from),
        wifiState: watch(WifiStatus, from),
        animationStatus: derived(animationState, ({ value, stale }) =>
            stale ? null : (value ?? null)
        )
    }
}

export const { modeState, gaitState, wifiState, animationStatus } = robotState()
