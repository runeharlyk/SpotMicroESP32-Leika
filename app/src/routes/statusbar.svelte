<script lang="ts">
	import { page } from '$app/stores';
	import { telemetry } from '$lib/stores/telemetry';
	import { openModal, closeModal } from 'svelte-modals';
	import ConfirmDialog from '$lib/components/ConfirmDialog.svelte';
	import WiFiOff from '~icons/tabler/wifi-off';
	import Hamburger from '~icons/tabler/menu-2';
	import Power from '~icons/tabler/power';
	import Cancel from '~icons/tabler/x';
	import RssiIndicator from '$lib/components/RSSIIndicator.svelte';
	import BatteryIndicator from '$lib/components/BatteryIndicator.svelte';
	import UpdateIndicator from '$lib/components/UpdateIndicator.svelte';
	import MdiWeatherSunny from '~icons/mdi/weather-sunny';
	import MdiMoonAndStars from '~icons/mdi/moon-and-stars';
	import MdiFullscreen from '~icons/mdi/fullscreen';
	import MdiFullscreenExit from '~icons/mdi/fullscreen-exit';
	import { api } from '$lib/api';
	import { isFullscreen, mode, modes, toggleFullscreen } from '$lib/stores';
	import Selector from '$lib/components/widget/Selector.svelte';
	import { selectedView, views } from '$lib/stores/application';

	const postSleep = async () => await api.post('/api/sleep');

	function confirmSleep() {
		openModal(ConfirmDialog, {
			title: 'Confirm Power Down',
			message: 'Are you sure you want to switch off the device?',
			labels: {
				cancel: { label: 'Abort', icon: Cancel },
				confirm: { label: 'Switch Off', icon: Power }
			},
			onConfirm: () => {
				closeModal();
				postSleep();
			}
		});
	}

	const deactivate = async () => {
		mode.set(modes.indexOf('deactivated'));
	};
</script>

<div class="navbar bg-base-300 sticky top-0 z-10 h-12 min-h-fit drop-shadow-lg lg:h-16 gap-2 pr-0">
	<div class="flex-1">
		<label for="main-menu" class="btn btn-ghost btn-circle btn-sm drawer-button"
			><Hamburger class="h-6 w-auto" /></label
		>
		{#if $page.data.title === 'Controller'}
			<Selector bind:selectedOption={$selectedView} options={$views.map((v) => v.name)} />
		{:else}
			<h1 class="px-2 text-xl font-bold lg:text-2xl">{$page.data.title}</h1>
		{/if}
	</div>
	<div class="indicator flex-none">
		<UpdateIndicator />
	</div>
	<!-- svelte-ignore a11y-click-events-have-key-events -->
	<!-- svelte-ignore a11y-no-static-element-interactions -->
	<div class="flex-none" on:click={toggleFullscreen}>
		<svelte:component this={isFullscreen ? MdiFullscreenExit : MdiFullscreen} class="h-7 w-7" />
	</div>

	<div class="flex-none">
		<label class="swap swap-rotate">
			<input type="checkbox" value="light" class="theme-controller" />
			<MdiWeatherSunny class="swap-off h-7 w-7" />
			<MdiMoonAndStars class="swap-on h-7 w-7" />
		</label>
	</div>
	<div class="flex-none">
		{#if $telemetry.rssi.disconnected}
			<WiFiOff class="h-7 w-7" />
		{:else}
			<RssiIndicator showDBm={false} rssi_dbm={$telemetry.rssi.rssi} class="h-7 w-7" />
		{/if}
	</div>

	{#if $page.data.features.battery}
		<div class="flex-none">
			<BatteryIndicator
				voltage={$telemetry.battery.voltage}
				current={$telemetry.battery.current}
				class="h-7 w-7"
			/>
		</div>
	{/if}

	{#if $page.data.features.sleep}
		<div class="flex-none">
			<button class="btn btn-square btn-ghost h-9 w-10" on:click={confirmSleep}>
				<Power class="text-error h-9 w-9" />
			</button>
		</div>
	{/if}
	<button on:click={deactivate} class="bg-error text-white btn rounded-none">STOP</button>
</div>
