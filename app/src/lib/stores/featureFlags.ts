import { notifications } from '$lib/components/toasts/notifications'
import Kinematic from '$lib/kinematic'
import { persistentStore } from '$lib/utilities'
import { derived, type Writable } from 'svelte/store'
import { resolve } from '$app/paths'
import { socket } from '$lib/stores'

let featureFlagsStore: Writable<Record<string, boolean | string>>

export function useFeatureFlags() {
    if (!featureFlagsStore) {
        featureFlagsStore = persistentStore<Record<string, boolean | string>>('FeatureFlags', {})

        socket
            .request({ featuresDataRequest: {} })
            .then(response => {
                if (response.featuresDataResponse) {
                    featureFlagsStore.set(
                        response.featuresDataResponse as unknown as Record<string, boolean | string>
                    )
                } else {
                    notifications.error('Feature flags could not be fetched', 2500)
                }
            })
            .catch(() => {
                notifications.error('Feature flags could not be fetched', 2500)
            })
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
    const variantFlag = $flagStore['variant'] as string
    return variantFlag && variants[variantFlag as keyof typeof variants] ?
            variants[variantFlag as keyof typeof variants]
        :   variants.SPOTMICRO_ESP32
})

export const currentKinematic = derived(
    currentVariant,
    $variant => new Kinematic($variant.kinematics)
)
