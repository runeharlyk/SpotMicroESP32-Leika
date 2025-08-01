import { api } from '$lib/api'
import { notifications } from '$lib/components/toasts/notifications'
import Kinematic from '$lib/kinematic'
import { persistentStore } from '$lib/utilities'
import { derived, type Writable } from 'svelte/store'
import { base } from '$app/paths'

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

export const variants = {
  SPOTMICRO_ESP32: {
    model: `${base}/spot_micro.urdf.xacro`,
    stl: `${base}/stl.zip`,
    kinematics: {
      l1: 60.5 / 100,
      l2: 10 / 100,
      l3: 111.7 / 100,
      l4: 118.5 / 100,
      L: 207.5 / 100,
      W: 78 / 100
    }
  },
  SPOTMICRO_YERTLE: {
    model: `${base}/yertle.URDF`,
    stl: `${base}/URDF.zip`,
    kinematics: {
      l1: 35 / 100,
      l2: 0 / 100,
      l3: 130 / 100,
      l4: 130 / 100,
      L: 240 / 100,
      W: 78 / 100
    }
  }
}

export const currentVariant = derived(useFeatureFlags(), $flagStore => {
  const variantFlag = $flagStore['variant'] as string
  return variantFlag && variants[variantFlag as keyof typeof variants] ?
      variants[variantFlag as keyof typeof variants]
    : variants.SPOTMICRO_ESP32
})

export const currentKinematic = derived(
  currentVariant,
  $variant => new Kinematic($variant.kinematics)
)
