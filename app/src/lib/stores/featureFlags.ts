import { api } from '$lib/api'
import { notifications } from '$lib/components/toasts/notifications'
import { persistentStore } from '$lib/utilities'
import { type Writable } from 'svelte/store'

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
