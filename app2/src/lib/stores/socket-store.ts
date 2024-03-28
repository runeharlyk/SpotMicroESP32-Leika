import { writable, type Writable } from 'svelte/store';
import { type angles } from '$lib/models';

export const isConnected = writable(false);
export const servoAngles: Writable<angles> = writable(new Int16Array(12).fill(0));
export const logs = writable([] as string[]);
export const battery = writable({});
export const mpu = writable({ heading: 0 });
export const distances = writable({});
export const settings = writable({});
export const systemInfo = writable({} as number);

export interface socketDataCollection {
	angles: Writable<angles>;
	logs: Writable<string[]>;
	battery: Writable<unknown>;
	mpu: Writable<unknown>;
	distances: Writable<unknown>;
	settings: Writable<unknown>;
	systemInfo: Writable<unknown>;
}

export const socketData = {
	angles: servoAngles,
	logs,
	battery,
	mpu,
	distances,
	settings,
	systemInfo
};
