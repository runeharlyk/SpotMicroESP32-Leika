<script lang="ts">
	import { onDestroy, onMount } from 'svelte';
	import { page } from '$app/stores';
	import { Modals, closeModal } from 'svelte-modals';
	import Toast from '$lib/components/toasts/Toast.svelte';
	import { notifications } from '$lib/components/toasts/notifications';
	import { fade } from 'svelte/transition';
	import '../app.css';
	import Menu from '../lib/components/menu.svelte';
	import Statusbar from '../lib/components/statusbar/statusbar.svelte';
	import Login from '../lib/components/login.svelte';
	import {
		telemetry,
		analytics,
		user,
		type UserProfile,
		ModesEnum,
		kinematicData,
		mode,
		outControllerData,
		servoAngles,
		servoAnglesOut,
		socket
	} from '$lib/stores';
	import type { Analytics, Battery, DownloadOTA } from '$lib/types/models';
	import { api } from '$lib/api';
    import { useFeatureFlags } from '$lib/stores/featureFlags';

    const features = useFeatureFlags();

	onMount(async () => {
		if ($user.bearer_token !== '') {
			await validateUser($user);
		}
		const ws_token = $features.security ? '?access_token=' + $user.bearer_token : '';
		socket.init(`ws://${window.location.host}/ws/events${ws_token}`);

		addEventListeners();

		outControllerData.subscribe((data) => socket.sendEvent('input', { data }));
		mode.subscribe((data) => socket.sendEvent('mode', { data }));
		servoAnglesOut.subscribe((data) => socket.sendEvent('angles', { data }));
		kinematicData.subscribe((data) => socket.sendEvent('position', { data }));
	});

	onDestroy(() => {
		removeEventListeners();
	});

	const addEventListeners = () => {
		socket.on('open', handleOpen);
		socket.on('close', handleClose);
		socket.on('error', handleError);
		socket.on('rssi', handleNetworkStatus);
		socket.on('mode', (data: ModesEnum) => mode.set(data));
		socket.on('angles', (angles: number[]) => {
			if (angles.length) servoAngles.set(angles);
		});
        features.subscribe(data => {
            if (data.analytics) socket.on('analytics', handleAnalytics);
            if (data.battery) socket.on('battery', handleBattery);
            if (data.download_firmware) socket.on('otastatus', handleOAT);
            if (data.sonar) socket.on('sonar', (data) => console.log(data));
        })
	};

	const removeEventListeners = () => {
		socket.off('analytics', handleAnalytics);
		socket.off('open', handleOpen);
		socket.off('close', handleClose);
		socket.off('rssi', handleNetworkStatus);
		socket.off('battery', handleBattery);
		socket.off('otastatus', handleOAT);
	};

	async function validateUser(userdata: UserProfile) {
		const result = await api.get('/api/verifyAuthorization');
		if (result.isErr()) {
			user.invalidate();
			console.error('Error:', result.inner);
		}
	}

	const handleOpen = () => {
		notifications.success('Connection to device established', 5000);
	};

	const handleClose = () => {
		notifications.error('Connection to device lost', 5000);
		telemetry.setRSSI(0);
	};

	const handleError = (data: any) => console.error(data);

	const handleAnalytics = (data: Analytics) => analytics.addData(data);

	const handleNetworkStatus = (data: number) => telemetry.setRSSI(data);

	const handleBattery = (data: Battery) => telemetry.setBattery(data);

	const handleOAT = (data: DownloadOTA) => telemetry.setDownloadOTA(data);

	let menuOpen = false;
</script>

<svelte:head>
	<title>{$page.data.title}</title>
</svelte:head>

{#if $features.security && $user.bearer_token === ''}
	<Login />
{:else}
	<div class="drawer">
		<input id="main-menu" type="checkbox" class="drawer-toggle" bind:checked={menuOpen} />
		<div class="drawer-content flex flex-col">
			<!-- Status bar content here -->
			<Statusbar />

			<!-- Main page content here -->
			<slot />
		</div>
		<!-- Side Navigation -->
		<div class="drawer-side z-30 shadow-lg">
			<label for="main-menu" class="drawer-overlay" />
			<Menu on:menuClicked={() => (menuOpen = false)} />
		</div>
	</div>
{/if}

<Modals>
	<!-- svelte-ignore a11y-click-events-have-key-events -->
	<!-- svelte-ignore a11y-no-static-element-interactions -->
	<div
		slot="backdrop"
		class="fixed inset-0 z-40 max-h-full max-w-full bg-black/20 backdrop-blur"
		transition:fade
		on:click={closeModal}
	/>
</Modals>

<Toast />
