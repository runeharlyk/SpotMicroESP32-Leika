import { writable } from 'svelte/store';

export const emulateModel = writable(true);

export const input = writable({left:{x:0, y:0}, right:{x:0, y:0}, height:70, speed:0});

export const outControllerData = writable(new Uint8Array([0, 128, 128, 128, 128, 70, 0]));
