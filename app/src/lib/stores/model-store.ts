import type { ControllerInput } from '$lib/models';
import { persistentStore } from '$lib/utilities/svelte-utilities';
import { writable, type Writable } from 'svelte/store';

export const emulateModel = writable(true);

export const jointNames = persistentStore('joint_names', []);

export const model = writable();

export const modes = ['deactivated', 'idle', 'calibration', 'rest', 'stand', 'crawl', 'walk'] as const;

export type Modes = (typeof modes)[number];

export enum ModesEnum {
	Deactivated,
	Idle,
	Calibration,
	Rest,
	Stand,
	Crawl,
	Walk
}

export const mode: Writable<ModesEnum> = writable(ModesEnum.Walk);

export const outControllerData = writable([0, 0, 0, 0, 0, 1, 0]);

export const kinematicData = writable([0, 0, 0, 0, 1, 0]);

export const input: Writable<ControllerInput> = writable({
	left: { x: 0, y: 0 },
	right: { x: 0, y: 0 },
	height: 50,
	speed: 50,
	s1: 50
});
