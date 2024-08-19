<script lang="ts">
	import { WiFi, WiFi0, WiFi1, WiFi2, WifiOff } from "../icons";

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
			<svelte:component this={getWiFiIcon()} class="absolute inset-0 h-full w-full" />
		</div>
	</div>
</div>