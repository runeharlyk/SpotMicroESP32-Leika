<script lang="ts">
	import WiFi from '~icons/tabler/wifi';
	import WiFi0 from '~icons/tabler/wifi-0';
	import WiFi1 from '~icons/tabler/wifi-1';
	import WiFi2 from '~icons/tabler/wifi-2';
    import WifiOff from '~icons/tabler/wifi-off';

	export let showDBm = true;
	export let rssi_dbm = 0;
</script>

<div class="indicator">
	<div class="tooltip tooltip-left" data-tip={rssi_dbm + " dBm"}>
		{#if showDBm}
			<span class="indicator-item indicator-start badge badge-accent badge-outline badge-xs">
				{rssi_dbm} dBm
			</span>
		{/if}
		{#if rssi_dbm >= -55}
			<WiFi class={$$props.class || ''} />
		{:else if rssi_dbm >= -75}
			<div class="{$$props.class || ''} relative">
				<WiFi class="absolute inset-0 h-full w-full opacity-30" />
				<WiFi2 class="absolute inset-0 h-full w-full" />
			</div>
		{:else if rssi_dbm >= -85}
			<div class="{$$props.class || ''} relative">
				<WiFi class="absolute inset-0 h-full w-full opacity-30" />
				<WiFi1 class="absolute inset-0 h-full w-full" />
			</div>
        {:else if rssi_dbm === 0}
            <WifiOff class={$$props.class || ''} />
		{:else}
			<div class="{$$props.class || ''} relative">
				<WiFi class="absolute inset-0 h-full w-full opacity-30" />
				<WiFi0 class="absolute inset-0 h-full w-full" />
			</div>
		{/if}
	</div>
</div>