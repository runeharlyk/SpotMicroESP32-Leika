import { persistentStore } from '$lib/utilities';
import Visualization from '$lib/components/Visualization.svelte';
import Stream from '$lib/components/Stream.svelte';
import CpuUsageChart from '$lib/components/widget/CpuUsageChart.svelte';
import type { Writable } from 'svelte/store';

export type SizeUnit = 'px' | '%' | 'fr' | 'em';

export enum SizeUnitEnum {
	Em = 'em',
	Fractional = 'fr',
	Percent = '%',
	Pixel = 'px'
}

export interface WidgetConfig {
	id: string | number;
	component: keyof typeof WidgetComponents;
	size?: number;
	sizeUnit?: SizeUnitEnum;
	props?: Record<string, any>;
}

export interface WidgetContainerConfig {
	id: string | number;
	layout: 'row' | 'column' | 'wrap';
	header?: string;
	size?: number;
	sizeUnit?: SizeUnitEnum;
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

export const controllerLayout: Writable<WidgetContainerConfig> = persistentStore('controller_layout', {
	id: 'root',
	layout: 'column',
	widgets: [
		{
			id: 'visualization',
			layout: 'column',
			header: 'Visualization',
			widgets: [
				{ id: 1, component: 'Stream' },
				{ id: 2, component: 'Visualization', props: { debug: true } }
			]
		},
		{
			id: 'charts',
			layout: 'row',
			size: 40,
			header: 'Charts',
			widgets: [
				{ id: 3, component: 'CpuUsageChart' },
				{ id: 4, component: 'CpuUsageChart' },
				{ id: 5, component: 'CpuUsageChart' }
			]
		}
	]
} as WidgetContainerConfig);
