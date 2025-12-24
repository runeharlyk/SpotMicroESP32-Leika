import { api } from '$lib/api'
import { notifications } from '$lib/components/toasts/notifications'
import Kinematic from '$lib/kinematic'
import { persistentStore } from '$lib/utilities'
import { derived, type Writable } from 'svelte/store'
import { resolve } from '$app/paths'

let featureFlagsStore: Writable<Record<string, boolean | string>>

export function useFeatureFlags() {
    if (!featureFlagsStore) {
        featureFlagsStore = persistentStore<Record<string, boolean | string>>('FeatureFlags', {})

        api.get<Record<string, boolean>>('/api/features').then(result => {
            if (result.isOk()) featureFlagsStore.set(result.inner)
            else {
                notifications.error('Feature flag could not be fetched', 2500)
            }
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
