import { writable, type Writable } from 'svelte/store';

export interface errorLog {
	message: unknown;
	tag?: string;
	exception?: unknown;
}

export const latestErrorLog: Writable<errorLog> = writable();

export const errorLogs: Writable<errorLog[]> = writable([]);
