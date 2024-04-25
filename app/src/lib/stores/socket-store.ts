import { writable, type Writable } from 'svelte/store';
import { type angles } from '$lib/models';

export const servoAnglesOut: Writable<number[]> = writable([
	0, 45, -90, 0, 45, -90, 0, 45, -90, 0, 45, -90
]);
export const servoAngles: Writable<number[]> = writable([
	0, 45, -90, 0, 45, -90, 0, 45, -90, 0, 45, -90
]);
export const logs = writable([] as string[]);
export const battery = writable({});
export const mpu = writable({ heading: 0 });
export const distances = writable({});

export interface socketDataCollection {
	angles: Writable<angles>;
	logs: Writable<string[]>;
	battery: Writable<unknown>;
	mpu: Writable<unknown>;
	distances: Writable<unknown>;
}

export const socketData = {
	angles: servoAngles,
	logs,
	battery,
	mpu,
	distances
};
