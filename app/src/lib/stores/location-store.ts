import { persistentStore } from '$lib/utilities';

export const location = persistentStore('location', '/');
