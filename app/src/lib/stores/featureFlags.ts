import { notifications } from '$lib/components/toasts/notifications'
import Kinematic from '$lib/kinematic'
import { persistentStore } from '$lib/utilities'
import { derived, get, type Writable } from 'svelte/store'
import { resolve } from '$app/paths'
import { socket } from './socket'
import { apiLocation } from './location-store'
import { identify } from './robots'
import type { FeaturesDataResponse } from '$lib/platform_shared/message'

let featureFlagsStore: Writable<Record<string, boolean | string>>

/** Records what the connected robot reported about itself: its feature flags and its identity. */
export function applyFeatures(features: FeaturesDataResponse) {
    useFeatureFlags().set(features as unknown as Record<string, boolean | string>)
    // A Bluetooth session has no network address to file the robot under.
    if (get(socket.transport) !== 'websocket') return
    identify(get(apiLocation) || window.location.host, {
        deviceId: features.deviceId,
        name: features.robotName,
        variant: features.variant
    })
}

// Each connection may be a different robot, so the flags are fetched again on every open.
const fetchFeatures = () =>
    socket
        .request({ featuresDataRequest: {} })
        .then(response => {
            if (response.featuresDataResponse) applyFeatures(response.featuresDataResponse)
            else notifications.error('Feature flags could not be fetched', 2500)
        })
        .catch(() => notifications.error('Feature flags could not be fetched', 2500))

export function useFeatureFlags() {
    if (!featureFlagsStore) {
        featureFlagsStore = persistentStore<Record<string, boolean | string>>('FeatureFlags', {})
        socket.onEvent('open', fetchFeatures)
        if (get(socket)) void fetchFeatures()
    }

    return featureFlagsStore
}

const base = resolve('/')

export const variants = {
    SPOTMICRO_ESP32: {
        model: `${base}spot_micro.urdf.xacro`,
        stl: `${base}stl.zip`,
        kinematics: {
            coxa: 0.0605,
            coxa_offset: 0.01,
            femur: 0.1112,
            tibia: 0.1185,
            L: 0.2075,
            W: 0.078
        }
    },
    // No Mini model exists yet, so the Pico is drawn with the full-size Spot Micro model.
    SPOTMICRO_ESP32_MINI: {
        model: `${base}spot_micro.urdf.xacro`,
        stl: `${base}stl.zip`,
        kinematics: {
            coxa: 0.035,
            coxa_offset: 0.0,
            femur: 0.06,
            tibia: 0.06,
            L: 0.16,
            W: 0.08
        }
    },
    SPOTMICRO_YERTLE: {
        model: `${base}yertle.URDF`,
        stl: `${base}URDF.zip`,
        kinematics: {
            coxa: 0.035,
            coxa_offset: 0.0,
            femur: 0.13,
            tibia: 0.13,
            L: 0.24,
            W: 0.078
        }
    }
}

export const currentVariant = derived(useFeatureFlags(), $flagStore => {
    const variantFlag = ($flagStore['variant'] as string)?.replace(/_V\d+$/, '')
    return variantFlag && variants[variantFlag as keyof typeof variants] ?
            variants[variantFlag as keyof typeof variants]
        :   variants.SPOTMICRO_ESP32
})

export const currentKinematic = derived(
    currentVariant,
    $variant => new Kinematic($variant.kinematics)
)
