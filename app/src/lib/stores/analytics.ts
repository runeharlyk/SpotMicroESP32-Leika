import { type Analytics } from '$lib/types/models';
import { writable } from 'svelte/store';

let analytics_data = {
	uptime: <number[]>[],
	free_heap: <number[]>[],
	total_heap: <number[]>[],
	used_heap: <number[]>[],
	min_free_heap: <number[]>[],
	max_alloc_heap: <number[]>[],
	fs_used: <number[]>[],
	fs_total: <number[]>[],
	core_temp: <number[]>[],
	cpu0_usage: <number[]>[],
	cpu1_usage: <number[]>[],
	cpu_usage: <number[]>[]
};

const maxAnalyticsData = 100;

function createAnalytics() {
	const { subscribe, update } = writable(analytics_data);

	return {
		subscribe,
		addData: (content: Analytics) => {
			update((analytics_data) => ({
				...analytics_data,
				uptime: [...analytics_data.uptime, content.uptime].slice(-maxAnalyticsData),
				free_heap: [...analytics_data.free_heap, content.free_heap / 1000].slice(-maxAnalyticsData),
				total_heap: [...analytics_data.total_heap, content.total_heap / 1000].slice(
					-maxAnalyticsData
				),
				used_heap: [
					...analytics_data.used_heap,
					(content.total_heap - content.free_heap) / 1000
				].slice(-maxAnalyticsData),
				min_free_heap: [...analytics_data.min_free_heap, content.min_free_heap / 1000].slice(
					-maxAnalyticsData
				),
				max_alloc_heap: [...analytics_data.max_alloc_heap, content.max_alloc_heap / 1000].slice(
					-maxAnalyticsData
				),
				fs_used: [...analytics_data.fs_used, content.fs_used / 1000].slice(-maxAnalyticsData),
				fs_total: [...analytics_data.fs_total, content.fs_total / 1000].slice(-maxAnalyticsData),
				core_temp: [...analytics_data.core_temp, content.core_temp].slice(-maxAnalyticsData),
				cpu0_usage: [...analytics_data.cpu0_usage, content.cpu0_usage].slice(-maxAnalyticsData),
				cpu1_usage: [...analytics_data.cpu1_usage, content.cpu1_usage].slice(-maxAnalyticsData),
				cpu_usage: [...analytics_data.cpu_usage, content.cpu_usage].slice(-maxAnalyticsData)
			}));
		}
	};
}

export const analytics = createAnalytics();
