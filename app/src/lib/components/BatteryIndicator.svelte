<script lang="ts">
	import Battery0 from '~icons/tabler/battery';
	import Battery25 from '~icons/tabler/battery-1';
	import Battery50 from '~icons/tabler/battery-2';
	import Battery75 from '~icons/tabler/battery-3';
	import Battery100 from '~icons/tabler/battery-4';
	import BatteryCharging from '~icons/tabler/battery-charging-2';

	export let current = 0;
	export let voltage = 0;
</script>

<div class="tooltip tooltip-left z-10" data-tip="{voltage}V {Math.floor(current*10)/10} mA">
	{#if voltage == 0}
		<BatteryCharging class="{$$props.class || ''} -rotate-90 animate-pulse" />
	{:else if voltage > 8.2}
		<Battery100 class="{$$props.class || ''} -rotate-90" />
	{:else if voltage > 8}
		<Battery75 class="{$$props.class || ''} -rotate-90" />
	{:else if voltage > 7.8}
		<Battery50 class="{$$props.class || ''} -rotate-90" />
	{:else if voltage > 7.6}
		<Battery25 class="{$$props.class || ''} -rotate-90" />
	{:else}
		<Battery0 class="{$$props.class || ''} text-error -rotate-90 animate-pulse" />
	{/if}
</div>
