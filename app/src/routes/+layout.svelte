<script lang="ts">
    import { onDestroy, onMount } from 'svelte';
    import { page } from '$app/stores';
    import { Modals, closeModal } from 'svelte-modals';
    import Toast from '$lib/components/toasts/Toast.svelte';
    import { notifications } from '$lib/components/toasts/notifications';
    import { fade } from 'svelte/transition';
    import '../app.css';
    import Menu from '../lib/components/menu/Menu.svelte';
    import Statusbar from '../lib/components/statusbar/statusbar.svelte';
    import {
        telemetry,
        analytics,
        ModesEnum,
        kinematicData,
        mode,
        outControllerData,
        servoAngles,
        servoAnglesOut,
        socket,
        location,
        useFeatureFlags
    } from '$lib/stores';
    import type { Analytics, DownloadOTA } from '$lib/types/models';

    const features = useFeatureFlags();

    onMount(async () => {
        const ws = $location ? $location : window.location.host;
        socket.init(`ws://${ws}/api/ws/events`);

        addEventListeners();

        outControllerData.subscribe(data => socket.sendEvent('input', { data }));
        mode.subscribe(data => socket.sendEvent('mode', { data }));
        servoAnglesOut.subscribe(data => socket.sendEvent('angles', { data }));
        kinematicData.subscribe(data => socket.sendEvent('position', { data }));
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
        socket.on('analytics', handleAnalytics);
        socket.on('angles', (angles: number[]) => {
            if (angles.length) servoAngles.set(angles);
        });
        features.subscribe(data => {
            if (data?.download_firmware) socket.on('otastatus', handleOAT);
            if (data?.sonar) socket.on('sonar', data => console.log(data));
        });
    };

    const removeEventListeners = () => {
        socket.off('analytics', handleAnalytics);
        socket.off('open', handleOpen);
        socket.off('close', handleClose);
        socket.off('rssi', handleNetworkStatus);
        socket.off('otastatus', handleOAT);
    };

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

    const handleOAT = (data: DownloadOTA) => telemetry.setDownloadOTA(data);

    let menuOpen = false;
</script>

<svelte:head>
    <title>{$page.data.title}</title>
</svelte:head>

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
