import type { Battery, DownloadOTA } from '$lib/types/models';
import { writable } from 'svelte/store';

let telemetry_data = {
	rssi: {
		rssi: 0
	},
	battery: {
		voltage: 0,
		current: 0
	},
	download_ota: {
		status: 'none',
		progress: 0,
		error: ''
	}
};

function createTelemetry() {
	const { subscribe, set, update } = writable(telemetry_data);

	return {
		subscribe,
		setRSSI: (data: number) => {
			update((telemetry_data) => ({
				...telemetry_data,
				rssi: { rssi: data }
			}));
		},
		setBattery: (data: Battery) => {
			update((telemetry_data) => ({
				...telemetry_data,
				battery: { voltage: data.voltage, current: data.current }
			}));
		},
		setDownloadOTA: (data: DownloadOTA) => {
			update((telemetry_data) => ({
				...telemetry_data,
				download_ota: { status: data.status, progress: data.progress, error: data.error }
			}));
		}
	};
}

export const telemetry = createTelemetry();
