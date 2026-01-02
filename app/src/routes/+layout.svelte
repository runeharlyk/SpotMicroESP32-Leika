<script lang="ts">
    import { onDestroy, onMount } from 'svelte'
    import { page } from '$app/state'
    import { Modals, modals } from 'svelte-modals'
    import Toast from '$lib/components/toasts/Toast.svelte'
    import { notifications } from '$lib/components/toasts/notifications'
    import { fade } from 'svelte/transition'
    import '../app.css'
    import Menu from '../lib/components/menu/Menu.svelte'
    import Statusbar from '../lib/components/statusbar/statusbar.svelte'
    import {
        telemetry,
        analytics,
        ModesEnum,
        kinematicData,
        mode,
        input,
        servoAngles,
        servoAnglesOut,
        socket,
        apiLocation,
        useFeatureFlags,
        walkGait
    } from '$lib/stores'
    import { type Analytics, type DownloadOTA } from '$lib/types/models'
    import { MessageTopic } from '$lib/types/models'
    import { Throttler } from '$lib/utilities'

    interface Props {
        children?: import('svelte').Snippet
    }

    let { children }: Props = $props()

    const features = useFeatureFlags()
    const throttler = new Throttler()

    onMount(async () => {
        const ws = $apiLocation ? $apiLocation : window.location.host
        socket.init(`ws://${ws}/api/ws`)

        addEventListeners()

        input.subscribe(data =>
            socket.sendEvent(
                MessageTopic.input,
                throttler.throttle(() => Object.values(data), 40)
            )
        )
        mode.subscribe(data => socket.sendEvent(MessageTopic.mode, data))
        walkGait.subscribe(data => socket.sendEvent(MessageTopic.gait, data))
        servoAnglesOut.subscribe(data =>
            throttler.throttle(() => socket.sendEvent(MessageTopic.angles, data), 100)
        )
        kinematicData.subscribe(data =>
            throttler.throttle(() => socket.sendEvent(MessageTopic.position, data), 100)
        )
    })

    onDestroy(() => {
        removeEventListeners()
    })

    const addEventListeners = () => {
        socket.on('open', handleOpen)
        socket.on('close', handleClose)
        socket.on('error', handleError)
        socket.on(MessageTopic.rssi, handleNetworkStatus)
        socket.on(MessageTopic.mode, (data: ModesEnum) => mode.set(data))
        socket.on(MessageTopic.analytics, handleAnalytics)
        socket.on(MessageTopic.angles, (angles: number[]) => {
            if (angles.length) servoAngles.set(angles)
        })
        features.subscribe(data => {
            if (data?.download_firmware) socket.on(MessageTopic.otastatus, handleOAT)
            if (data?.sonar) socket.on(MessageTopic.sonar, data => console.log(data))
        })
    }

    const removeEventListeners = () => {
        socket.off(MessageTopic.analytics, handleAnalytics)
        socket.off('open', handleOpen)
        socket.off('close', handleClose)
        socket.off(MessageTopic.rssi, handleNetworkStatus)
        socket.off(MessageTopic.otastatus, handleOAT)
    }

    const handleOpen = () => {
        notifications.success('Connection to device established', 5000)
    }

    const handleClose = () => {
        notifications.error('Connection to device lost', 5000)
        telemetry.setRSSI(0)
    }

    const handleError = (data: unknown) => console.error(data)

    const handleAnalytics = (data: Analytics) => analytics.addData(data)

    const handleNetworkStatus = (data: number) => telemetry.setRSSI(data)

    const handleOAT = (data: DownloadOTA) => telemetry.setDownloadOTA(data)

    let menuOpen = $state(false)
</script>

<svelte:head>
    <title>{page.data.title}</title>
</svelte:head>

<div class="drawer">
    <input id="main-menu" type="checkbox" class="drawer-toggle" bind:checked={menuOpen} />
    <div class="drawer-content flex flex-col">
        <!-- Status bar content here -->
        <Statusbar />

        <!-- Main page content here -->
        {@render children?.()}
    </div>
    <!-- Side Navigation -->
    <div class="drawer-side z-30 shadow-lg">
        <label for="main-menu" class="drawer-overlay"></label>
        <Menu menuClicked={() => (menuOpen = false)} />
    </div>
</div>

<Modals>
    {#snippet backdrop()}
        <div
            class="fixed inset-0 z-40 max-h-full max-w-full bg-black/20 backdrop-blur-sm"
            transition:fade
            onclick={modals.closeAll}
            onkeydown={e => e.key === 'Escape' && modals.closeAll()}
            role="button"
            tabindex="0"
        ></div>
    {/snippet}
</Modals>

<Toast />
