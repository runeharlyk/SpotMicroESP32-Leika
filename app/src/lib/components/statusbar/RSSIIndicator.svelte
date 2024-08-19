<script lang="ts">
	import WiFi from '~icons/tabler/wifi';
	import WiFi0 from '~icons/tabler/wifi-0';
	import WiFi1 from '~icons/tabler/wifi-1';
	import WiFi2 from '~icons/tabler/wifi-2';
    import WifiOff from '~icons/tabler/wifi-off';

	export let showDBm = false;
	export let rssi = 0;

	const getWiFiIcon = () => {
		if (rssi === 0) return WifiOff;
		if (rssi >= -55) return WiFi;
		if (rssi >= -75) return WiFi2;
		if (rssi >= -85) return WiFi1;
		return WiFi0;
	};
</script>

<div class="indicator">
	<div class="tooltip tooltip-left" data-tip={rssi + " dBm"}>
		{#if showDBm}
			<span class="indicator-item indicator-start badge badge-accent badge-outline badge-xs">
				{rssi} dBm
			</span>
		{/if}
		<div class="h-7 w-7">
			{#if rssi !== 0 && rssi < -55}
				<WiFi class="absolute inset-0 h-full w-full opacity-30" />
			{/if}
			<svelte:component this={getWiFiIcon()} class="absolute inset-0 h-full w-full" />
		</div>
	</div>
</div>