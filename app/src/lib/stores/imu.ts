import { type IMU } from '$lib/types/models';
import { writable } from 'svelte/store';

let imu_data = {
	x: <number[]>[],
	y: <number[]>[],
	z: <number[]>[],
	temp: <number[]>[]
};

const maxIMUData = 100;

function createIMU() {
	const { subscribe, update } = writable(imu_data);

	return {
		subscribe,
		addData: (content: IMU) => {
			update((imu_data) => ({
				...imu_data,
				x: [...imu_data.x, content.x].slice(-maxIMUData),
				y: [...imu_data.y, content.y].slice(-maxIMUData),
				z: [...imu_data.z, content.z].slice(-maxIMUData),
				temp: [...imu_data.temp, content.temp].slice(-maxIMUData)
			}));
		}
	};
}

export const imu = createIMU();
