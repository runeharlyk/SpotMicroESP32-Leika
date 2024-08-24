import { persistentStore } from '$lib/utilities';
import { writable } from 'svelte/store';
import appEnv from 'app-env';

export const location = appEnv.VITE_USE_HOST_NAME ? writable('') : persistentStore('location', '');
