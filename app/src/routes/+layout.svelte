<script lang="ts">
    import { onDestroy, onMount } from 'svelte'
    import { get } from 'svelte/store'
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
        kinematicData,
        mode,
        input,
        servoAngles,
        servoAnglesOut,
        socket,
        robotSocketUrl,
        apiLocation,
        canReachRobot,
        walkGait
    } from '$lib/stores'
    import {
        AnglesData,
        ControllerData,
        KinematicData,
        ModeData,
        RSSIData,
        WalkGaitData
    } from '$lib/platform_shared/message'
    import { Throttler } from '$lib/utilities'
    import { keepControlAlive, stopped } from '$lib/control-link'

    interface Props {
        children?: import('svelte').Snippet
    }

    let { children }: Props = $props()

    // One per stream, so a burst of servo angles cannot hold back the next joystick command.
    const inputThrottler = new Throttler()
    const anglesThrottler = new Throttler()

    onMount(async () => {
        if (canReachRobot(page.url, $apiLocation)) socket.init(robotSocketUrl())

        addEventListeners()
        document.addEventListener('visibilitychange', handleVisibilityChange)

        input.subscribe(data =>
            inputThrottler.throttle(() => socket.emit(ControllerData, data), 100)
        )
        stopKeepAlive = keepControlAlive(
            () => get(input),
            data => socket.emit(ControllerData, data)
        )
        mode.subscribe(data => socket.emit(ModeData, data))
        walkGait.subscribe(data => socket.emit(WalkGaitData, data))
        servoAnglesOut.subscribe(data =>
            anglesThrottler.throttle(() => socket.emit(AnglesData, data), 100)
        )
        kinematicData.subscribe(data => socket.emit(KinematicData, data))
    })

    let stopKeepAlive: (() => void) | undefined

    onDestroy(() => {
        stopKeepAlive?.()
        removeEventListeners()
        document.removeEventListener('visibilitychange', handleVisibilityChange)
    })

    const eventListeners: (() => void)[] = []
    const addEventListeners = () => {
        eventListeners.push(
            socket.onEvent('open', handleOpen),
            // A link ends in exactly one of these; the transport is detached at the first.
            socket.onEvent('close', handleClose),
            socket.onEvent('error', handleClose),
            socket.onEvent('unresponsive', handleClose),
            socket.onEvent('error', handleError),
            socket.on(RSSIData, data => telemetry.setRSSI(data)),
            socket.on(ModeData, data => mode.set(data)),
            socket.on(AnglesData, data => {
                servoAngles.set(data)
            })
        )
    }

    const removeEventListeners = () => {
        eventListeners.forEach(offFunction => offFunction())
    }

    const handleOpen = () => notifications.success('Connection to device established', 5000)

    const handleClose = () => {
        notifications.error('Connection to device lost', 5000)
        telemetry.setRSSI(RSSIData.create({ rssi: 0 }))
        input.update(stopped)
    }

    // A backgrounded tab stops driving the input store, so the robot would hold the last commanded
    // gait. Emit the neutral command directly: the throttled path defers into a timer the browser
    // also throttles, and drops the call outright if a send is already pending.
    const handleVisibilityChange = () => {
        if (!document.hidden) return
        const neutral = stopped(get(input))
        input.set(neutral)
        socket.emit(ControllerData, neutral)
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

<svelte:window onkeydown={e => e.key === 'Escape' && modals.closeAll()} />

<Modals>
    {#snippet backdrop()}
        <div
            class="fixed inset-0 z-40 max-h-full max-w-full bg-black/20 backdrop-blur-sm"
            transition:fade
            onclick={modals.closeAll}
            role="presentation"
        ></div>
    {/snippet}
</Modals>

<Toast />
