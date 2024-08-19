import { api } from '$lib/api';
import { notifications } from '$lib/components/toasts/notifications';
import { writable, type Writable } from 'svelte/store';

let featureFlagsStore: Writable<Record<string, boolean>>;

export function useFeatureFlags() {
	if (!featureFlagsStore) {
		featureFlagsStore = writable<Record<string, boolean>>({});

		api.get<Record<string, boolean>>('/api/features').then((result) => {
			if (result.isOk()) featureFlagsStore.set(result.inner);
			else {
				notifications.error('Feature flag could not be fetched', 2500);
			}
		});
	}

	return featureFlagsStore;
}