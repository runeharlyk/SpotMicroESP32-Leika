import { notifications } from '$lib/components/toasts/notifications'
import Kinematic from '$lib/kinematic'
import { persistentStore } from '$lib/utilities'
import { derived, get, writable, type Writable } from 'svelte/store'
import { resolve } from '$app/paths'
import { socket } from './socket'
import { apiLocation } from './location-store'
import { identify } from './robots'
import { VARIANT_DIMENSIONS, knownVariant } from '$lib/kinematics-variants'
import type { FeaturesDataResponse } from '$lib/platform_shared/message'

let featureFlagsStore: Writable<Record<string, boolean | string>>

/**
 * What the robot on the open connection reported; null until it answers on this connection. The
 * persisted flags may still be the previous robot's, so a decision about this robot waits for these.
 */
export const connectionFeatures = writable<FeaturesDataResponse | null>(null)

/** Records what the connected robot reported about itself: its feature flags and its identity. */
export function applyFeatures(features: FeaturesDataResponse) {
    connectionFeatures.set(features)
    useFeatureFlags().set(features as unknown as Record<string, boolean | string>)
    // A Bluetooth session has no network address to file the robot under.
    if (get(socket.transport) !== 'websocket') return
    identify(get(apiLocation) || window.location.host, {
        deviceId: features.deviceId,
        name: features.robotName,
        variant: features.variant,
        hostname: features.hostname
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
        socket.subscribe(open => {
            if (!open) connectionFeatures.set(null)
        })
        socket.onEvent('open', fetchFeatures)
        if (get(socket)) void fetchFeatures()
    }

    return featureFlagsStore
}

const base = resolve('/')

/**
 * `modelYaw` turns a model whose forward axis differs from Spot Micro's. `drivable` is false where
 * the robot's joint angles cannot yet be mapped onto the model, which then holds its CAD stance.
 */
export const variants = {
    SPOTMICRO_ESP32: {
        model: `${base}spot_micro.urdf.xacro`,
        stl: `${base}stl.zip`,
        modelYaw: 0,
        drivable: true,
        kinematics: VARIANT_DIMENSIONS.SPOTMICRO_ESP32
    },
    // The Pico's model is generated from the simulation (scripts/build_sim_models.js). Its URDF and
    // these firmware dimensions disagree by up to 16 mm, so it is not driven until one is confirmed.
    SPOTMICRO_ESP32_MINI: {
        model: `${base}spot_pico.urdf`,
        stl: `${base}spot_pico.zip`,
        modelYaw: Math.PI / 2,
        drivable: false,
        kinematics: VARIANT_DIMENSIONS.SPOTMICRO_ESP32_MINI
    },
    SPOTMICRO_YERTLE: {
        model: `${base}yertle.URDF`,
        stl: `${base}URDF.zip`,
        modelYaw: 0,
        drivable: true,
        kinematics: VARIANT_DIMENSIONS.SPOTMICRO_YERTLE
    }
}

/** The variant the last connected robot reported, when it is one this app knows. */
export const reportedVariant = derived(useFeatureFlags(), $flagStore =>
    knownVariant($flagStore['variant'] as string | undefined)
)

/** Until the robot knows which variant it is, it refuses to move and asks to be told. */
export const needsVariantChoice = (
    connected: boolean,
    features: Pick<FeaturesDataResponse, 'variant'> | null
) => connected && features !== null && features.variant === ''

export const variantChoiceNeeded = derived([socket, connectionFeatures], ([$socket, $features]) =>
    needsVariantChoice($socket, $features)
)

export const currentVariant = derived(
    reportedVariant,
    $variant => variants[$variant ?? 'SPOTMICRO_ESP32']
)

export const currentKinematic = derived(
    currentVariant,
    $variant => new Kinematic($variant.kinematics)
)
