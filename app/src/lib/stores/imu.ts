import { type IMU } from '$lib/types/models';
import { writable } from 'svelte/store';

let imu_data = {
	x: <number[]>[],
	y: <number[]>[],
	z: <number[]>[],
	imu_temp: <number[]>[],
	altitude: <number[]>[],
	pressure: <number[]>[],
	bmp_temp: <number[]>[]
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
				imu_temp: [...imu_data.imu_temp, content.imu_temp].slice(-maxIMUData),
				altitude: [...imu_data.altitude, content.altitude].slice(-maxIMUData),
				pressure: [...imu_data.pressure, content.pressure].slice(-maxIMUData),
				bmp_temp: [...imu_data.bmp_temp, content.bmp_temp].slice(-maxIMUData)
			}));
		}
	};
}

export const imu = createIMU();
