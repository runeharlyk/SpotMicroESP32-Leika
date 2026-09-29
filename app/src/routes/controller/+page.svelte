<script lang="ts">
    import Controls from './Controls.svelte'
    import WidgetContainer from '$lib/components/layout/WidgetContainer.svelte'
    import { selectedView, views } from '$lib/stores/application'
    import { onDestroy, onMount } from 'svelte'
    import { mpu, socket } from '$lib/stores'
    import { imu } from '$lib/stores/imu'
    import { IMUData } from '$lib/platform_shared/message'

    let layout = $derived($views.find(v => v.name === $selectedView)!)

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

<div class="absolute top-0 select-none w-screen h-dvh">
    <Controls />
    <div class="absolute w-full h-dvh top-0 overflow-hidden lg:pt-16 pt-12">
        <WidgetContainer container={layout.content} />
    </div>
</div>
