import { writable } from 'svelte/store';

export const isEmbeddedApp = import.meta.env.VITE_EMBEDDED_BUILD === 'true';

export const persistentStore = (key: string, initialValue: any) => {
	const savedValue = JSON.parse(localStorage.getItem(key) as string);
	const data = savedValue !== null ? savedValue : initialValue;
	const store = writable(data);

	store.subscribe((value) => {
		localStorage.setItem(key, JSON.stringify(value));
	});

	return store;
};
