import { writable } from 'svelte/store';

export const isFullscreen = writable(false);

export function toggleFullscreen() {
	isFullscreen.update((state) => {
		!state ? document.documentElement.requestFullscreen() : document.exitFullscreen();
		return !state;
	});
}

export function enterFullscreen() {
	if (!document.fullscreenElement) {
		document.documentElement.requestFullscreen();
		isFullscreen.set(true);
	}
}

export function exitFullscreen() {
	if (document.fullscreenElement) {
		document.exitFullscreen();
		isFullscreen.set(false);
	}
}
