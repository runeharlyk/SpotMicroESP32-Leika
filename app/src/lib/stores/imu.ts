import { writable } from 'svelte/store';
import type { IMU } from '$lib/types/models';

const maxIMUData = 100;

export const imu = (() => {
    const { subscribe, update } = writable({
        x: [] as number[],
        y: [] as number[],
        z: [] as number[],
        heading: [] as number[],
        altitude: [] as number[],
        pressure: [] as number[],
        bmp_temp: [] as number[]
    });

    const addData = (content: IMU) => {
        update(data => {
            (Object.keys(content) as (keyof IMU)[]).forEach(key => {
                data[key] = [...data[key], content[key]].slice(-maxIMUData);
            });
            return data;
        });
    };

    return { subscribe, addData };
})();
