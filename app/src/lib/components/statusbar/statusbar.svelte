<script lang="ts">
	import { page } from '$app/stores';
	import { telemetry } from '$lib/stores/telemetry';
	
	import RssiIndicator from '$lib/components/statusbar/RSSIIndicator.svelte';
	import BatteryIndicator from '$lib/components/statusbar/BatteryIndicator.svelte';
	import UpdateIndicator from '$lib/components/statusbar/UpdateIndicator.svelte';
	import SleepButton from './SleepButton.svelte';
	import ThemeButton from './ThemeButton.svelte';
	import FullscreenButton from './FullscreenButton.svelte';
	import StopButton from './StopButton.svelte';
	import ViewSelector from './ViewSelector.svelte';
	import { Hamburger } from '../icons';
</script>

<div class="navbar bg-base-300 sticky top-0 z-10 h-12 min-h-fit drop-shadow-lg lg:h-16 gap-2 pr-0">
	<div class="flex-1">
		<label for="main-menu" class="btn btn-ghost btn-circle btn-sm drawer-button">
            <Hamburger class="h-6 w-auto" />
        </label>
		{#if $page.data.title === 'Controller'}
			<ViewSelector />
		{:else}
			<h1 class="px-2 text-xl font-bold lg:text-2xl">{$page.data.title}</h1>
		{/if}
    </div>
	
    <UpdateIndicator />
	
    <FullscreenButton />

    <ThemeButton/>

    <RssiIndicator rssi={$telemetry.rssi.rssi}/>
	
    <BatteryIndicator battery={$telemetry.battery} />
	
    <SleepButton />
	
	<StopButton />
</div>
