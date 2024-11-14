<script lang="ts">
    import Controls from './Controls.svelte';
    import WidgetContainer from '$lib/components/layout/WidgetContainer.svelte';
    import { selectedView, views } from '$lib/stores/application';
    import { onMount } from 'svelte';
    import { mpu, socket } from '$lib/stores';
    import { imu } from '$lib/stores/imu';
    import type { IMU } from '$lib/types/models';

    $: layout = $views.find(v => v.name === $selectedView)!;

    onMount(() => {
        socket.on('imu', (data: IMU) => {
            imu.addData(data);
            if (data.heading)
                mpu.update(mpuData => {
                    mpuData.heading = data.heading;
                    console.log(data.heading);

                    return mpuData;
                });
        });
    });
</script>

<div class="absolute top-0 select-none w-screen h-screen">
    <Controls />
    <div class="absolute w-full h-screen top-0 overflow-hidden lg:pt-16 pt-12">
        <WidgetContainer container={layout.content} />
    </div>
</div>
