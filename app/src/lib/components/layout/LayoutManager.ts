import { persistentStore } from '$lib/utilities';
import Visualization from '$lib/components/Visualization.svelte';
import Stream from '$lib/components/Stream.svelte';
import CpuUsageChart from '$lib/components/widget/CpuUsageChart.svelte';
import type { Writable } from 'svelte/store';

export interface WidgetConfig {
	id: string | number;
	component: keyof typeof WidgetComponents;
	size?: number;
	props?: Record<string, any>;
}

export interface WidgetContainerConfig {
	id: string | number;
	layout: 'row' | 'column' | 'wrap';
	header?: string;
	size?: number;
	widgets: Array<WidgetConfig | WidgetContainerConfig>;
}

export const isWidgetConfig = (
	widget: WidgetConfig | WidgetContainerConfig
): widget is WidgetConfig => 'component' in widget;

export const WidgetComponents = {
	Visualization,
	Stream,
	CpuUsageChart
};

export const phoneControllerLayout: Writable<WidgetContainerConfig> = persistentStore(
	'phone_controller_layout',
	{
		id: 'root',
		layout: 'wrap',
		widgets: [
			{ id: 2, component: 'Stream' },
			{ id: 1, component: 'Visualization', props: { debug: true } }
		]
	} as WidgetContainerConfig
);

export const controllerLayout: Writable<WidgetContainerConfig> = persistentStore(
	'controller_layout',
	{
		id: 'root',
		layout: 'column',
		widgets: [
			{
				id: 'visualization',
				layout: 'column',
				header: 'Visualization',
				size: 2,
				widgets: [
					{ id: 1, component: 'Visualization', size: 2, props: { debug: true } },
					{ id: 2, component: 'Stream' }
				]
			},
			{
				id: 'charts',
				layout: 'row',
				header: 'Charts',
				widgets: [
					{ id: 3, component: 'CpuUsageChart' },
					{ id: 4, component: 'CpuUsageChart' },
					{ id: 5, component: 'CpuUsageChart' }
				]
			}
		]
	} as WidgetContainerConfig
);
