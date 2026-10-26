<script lang="ts">
    import Controls from './Controls.svelte'
    import WidgetContainer from '$lib/components/layout/WidgetContainer.svelte'
    import { page } from '$app/state'
    import {
        isWidgetConfig,
        offlineView,
        selectedView,
        views,
        type WidgetContainerConfig
    } from '$lib/stores/application'
    import { onDestroy, onMount } from 'svelte'
    import { apiLocation, hasRobot, mpu, socket } from '$lib/stores'
    import { driveSimulation } from '$lib/stores/drive'
    import { imu } from '$lib/stores/imu'
    import { IMUData } from '$lib/platform_shared/message'
    import { HAS_3D_VIEW } from '$lib/build-flags'

    const hasSimulation = (container: WidgetContainerConfig): boolean =>
        container.widgets.some(widget =>
            isWidgetConfig(widget) ? widget.component === 'Simulation' : hasSimulation(widget)
        )

    const transport = socket.transport
    const robot = $derived(hasRobot(page.url, $apiLocation, $transport))
    const viewName = $derived(robot ? $selectedView : $offlineView)
    let layout = $derived($views.find(v => v.name === viewName) ?? $views[0])
    // The simulation is a robot of its own: while the layout shows it, the controls drive it and never the real robot.
    const simulated = $derived(HAS_3D_VIEW && hasSimulation(layout.content))

    $effect(() => {
        driveSimulation(simulated)
        return () => driveSimulation(false)
    })

    let stopImu: (() => void) | undefined
    onMount(() => {
        stopImu = socket.on(IMUData, (data: IMUData) => {
            imu.addData(data)
            if (data.heading)
                mpu.update(mpuData => {
                    mpuData.heading = data.heading
                    return mpuData
                })
        })
    })
    onDestroy(() => stopImu?.())
</script>

<div class="absolute top-0 select-none w-full h-dvh">
    <Controls />
    <div class="absolute w-full h-dvh top-0 overflow-hidden lg:pt-16 pt-12">
        <WidgetContainer container={layout.content} />
    </div>
</div>
