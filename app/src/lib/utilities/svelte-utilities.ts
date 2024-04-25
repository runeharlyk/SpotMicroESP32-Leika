import { writable } from 'svelte/store';
import { browser } from '$app/environment';

export const isEmbeddedApp = import.meta.env.VITE_EMBEDDED_BUILD === 'true';

export const persistentStore = (key: string, initialValue: any) => {
	const savedValue = browser ? JSON.parse(localStorage.getItem(key) as string) : null;
	const data = savedValue !== null ? savedValue : initialValue;
	const store = writable(data);

	store.subscribe((value) => {
		browser && localStorage.setItem(key, JSON.stringify(value));
	});

	return store;
};
