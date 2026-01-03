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
    import {
        AnalyticsData,
        AnglesData,
        DownloadOTAData,
        HumanInputData,
        KinematicData,
        ModeData,
        PingMsg,
        RSSIData,
        SonarData,
        WalkGaitData
    } from '$lib/platform_shared/message'
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
            throttler.throttle(() => socket.sendEvent(HumanInputData, data), 100)
        )
        mode.subscribe(data => socket.sendEvent(ModeData, data))
        walkGait.subscribe(data => socket.sendEvent(WalkGaitData, data))
        servoAnglesOut.subscribe(data =>
            throttler.throttle(() => socket.sendEvent(AnglesData, data), 100)
        )
        kinematicData.subscribe(data => socket.sendEvent(KinematicData, data))
    })

    onDestroy(() => {
        removeEventListeners()
    })

    const eventListeners: (() => void)[] = []
    const addEventListeners = () => {
        eventListeners.push(
            ...[
                socket.onEvent('open', handleOpen),
                socket.onEvent('close', handleClose),
                socket.onEvent('error', handleError),
                socket.on(RSSIData, data => telemetry.setRSSI(data)),
                socket.on(ModeData, data => mode.set(data)),
                socket.on(AnalyticsData, data => {
                    // console.log(data);
                    analytics.addData(data)
                }),
                socket.on(AnglesData, data => {
                    servoAngles.set(data)
                }),
                socket.on(PingMsg, data => {
                    console.log('Ping received!')
                })
            ]
        )
        features.subscribe(data => {
            if (data?.download_firmware)
                eventListeners.push(
                    socket.on(DownloadOTAData, data => telemetry.setDownloadOTA(data))
                )
            if (data?.sonar) eventListeners.push(socket.on(SonarData, data => console.log(data)))
        })
    }

    const removeEventListeners = () => {
        for (let offFunction of eventListeners) {
            offFunction()
        }
    }

    const handleOpen = () => {
        notifications.success('Connection to device established', 5000)
    }

    const handleClose = () => {
        notifications.error('Connection to device lost', 5000)
        telemetry.setRSSI(RSSIData.create({ rssi: 0 }))
    }

    const handleError = (data: unknown) => console.error(data)

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
