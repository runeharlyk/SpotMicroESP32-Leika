<script lang="ts">
	import { useFeatureFlags } from '$lib/stores';
	import type { Battery } from '$lib/types/models';
	import { BatteryCharging, Battery100, Battery75, Battery50, Battery25, Battery0 } from '../icons';

	const features = useFeatureFlags();

	export let battery:Battery;

	const getBatteryIcon = () => {
		if (battery.voltage === 0) return BatteryCharging;
		if (battery.voltage > 8.2) return Battery100;
		if (battery.voltage > 8) return Battery75;
		if (battery.voltage > 7.8) return Battery50;
		if (battery.voltage > 7.6) return Battery25;
		return Battery0;
	};
</script>

{#if $features.battery}
	<div class="tooltip tooltip-left z-10" data-tip="{battery.voltage}V {Math.floor(battery.current*10)/10} mA">
		<svelte:component 
			this={getBatteryIcon()} 
			class="h-7 w-7 -rotate-90 {battery.voltage === 0 || battery.voltage <= 7.6 ? 'animate-pulse' : ''} {battery.voltage <= 7.6 ? 'text-error' : ''}" 
		/>
	</div>
{/if}
