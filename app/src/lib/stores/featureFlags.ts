import { api } from '$lib/api';
import { notifications } from '$lib/components/toasts/notifications';
import { onMount } from 'svelte';
import { writable } from 'svelte/store';

export function useFeatureFlags() {
	const featureFlags = writable<Record<string, boolean>>({});
	onMount(async () => {
		const result = await api.get<Record<string, boolean>>('/api/features');
		if (result.isOk()) featureFlags.set(result.inner);
		else {
			notifications.error('Feature flag could not fetched', 2500);
		}
	});

	return featureFlags;
}
