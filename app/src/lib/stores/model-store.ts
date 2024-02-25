import type { ControllerInput } from '$lib/models';
import { persistentStore } from '$lib/utilities';
import { writable, type Writable } from 'svelte/store';

export const emulateModel = writable(true);

export const jointNames = persistentStore('joint_names', []);

export const model = writable();

export const modes = ['idle', 'rest', 'stand', 'walk'] as const;

export type Modes = (typeof modes)[number];

export const mode: Writable<Modes> = writable('idle');

export const outControllerData = writable(new Int8Array([0, 0, 0, 0, 0, 70, 0]));

export const input: Writable<ControllerInput> = writable({
	left: { x: 0, y: 0 },
	right: { x: 0, y: 0 },
	height: 70,
	speed: 0
});
