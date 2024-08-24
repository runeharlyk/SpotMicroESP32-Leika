import { writable } from 'svelte/store';
import { browser } from '$app/environment';

export const persistentStore = <T>(key: string, initialValue: T) => {
	const savedValue = browser ? localStorage.getItem(key) : null;
	const data: T = savedValue !== null ? JSON.parse(savedValue) : initialValue;
	const store = writable<T>();

	store.subscribe(value => {
		if (browser) localStorage.setItem(key, JSON.stringify(value));
	});

	store.set(data);

	return store;
};
