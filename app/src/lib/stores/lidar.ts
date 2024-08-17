import { writable } from 'svelte/store';

export type LidarPoint = {
	distance: number;
	angle: number;
	quality: number;
};

let lidar_data = {
	points: <LidarPoint[]>[]
};

const maxLidarData = 600;

function createLidar() {
	const { subscribe, update } = writable(lidar_data);

	return {
		subscribe,
		addData: (lidarPoint: LidarPoint) => {
			update((lidar_data) => ({
				...lidar_data,
				points: [...lidar_data.points, lidarPoint].slice(-maxLidarData)
			}));
		}
	};
}

export const lidar = createLidar();
