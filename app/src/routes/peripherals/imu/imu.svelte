<script lang="ts">
	import SettingsCard from "$lib/components/SettingsCard.svelte";
    import Rotate3d from '~icons/mdi/rotate-3d';
	import IMUSetting from './imuSetting.svelte';
    import { imu } from '$lib/stores/imu';
    import { Chart, registerables } from 'chart.js';
	import { cubicOut } from "svelte/easing";
	import { slide } from "svelte/transition";
	import { onDestroy, onMount } from "svelte";
	import { daisyColor } from "$lib/DaisyUiHelper";
	import { socket } from "$lib/stores";
	import type { IMU } from "$lib/types/models";

    Chart.register(...registerables);

    let heapChartElement: HTMLCanvasElement;
	let heapChart: Chart;

    
    const handleImu = (data: IMU) => imu.addData(data);   

    onMount(() => {
        socket.on('imu', handleImu);
        heapChart = new Chart(heapChartElement, {
			type: 'line',
			data: {
				// labels: $imu.x,
				datasets: [
					{
						label: 'x',
						borderColor: daisyColor('--p'),
						backgroundColor: daisyColor('--p', 50),
						borderWidth: 2,
						data: $imu.x,
						yAxisID: 'x'
					},
					{
						label: 'y',
						borderColor: daisyColor('--s'),
						backgroundColor: daisyColor('--s', 50),
						borderWidth: 2,
						data: $imu.y,
						yAxisID: 'y'
					},
                    {
						label: 'z',
						borderColor: daisyColor('--a'),
						backgroundColor: daisyColor('--a', 50),
						borderWidth: 2,
						data: $imu.z,
						yAxisID: 'z'
					}
				]
			},
			options: {
				maintainAspectRatio: false,
				responsive: true,
				plugins: {
					legend: {
						display: true
					},
					tooltip: {
						mode: 'index',
						intersect: false
					}
				},
				elements: {
					point: {
						radius: 1
					}
				},
				scales: {
					x: {
						grid: {
							color: daisyColor('--bc', 10)
						},
						ticks: {
							color: daisyColor('--bc')
						},
						display: false
					},
					y: {
						type: 'linear',
						title: {
							display: true,
							text: 'Angle [°]',
							color: daisyColor('--bc'),
							font: {
								size: 16,
								weight: 'bold'
							}
						},
						position: 'left',
						min:  0,
						max: 10,
						grid: { color: daisyColor('--bc', 10) },
						ticks: { color: daisyColor('--bc') },
						border: { color: daisyColor('--bc', 10) }
					}
				}
			}
		});
        setInterval(() => {
			updateData(), 200;
		});
    })

    onDestroy(() => {
        socket.off('imu', handleImu);
    })

    const updateData = () => {
        // heapChart.data.labels = $imu.x;
        heapChart.options.scales!.y!.min = Math.min(Math.min(...$imu.x), Math.min(...$imu.y), Math.min(...$imu.z));
        heapChart.options.scales!.y!.max = Math.max(Math.max(...$imu.x), Math.max(...$imu.y), Math.max(...$imu.z));
		heapChart.data.datasets[0].data = $imu.x;
		heapChart.data.datasets[1].data = $imu.y;
		heapChart.data.datasets[2].data = $imu.z;
		heapChart.update('none');
    }

</script>

<SettingsCard collapsible={false}>
    <Rotate3d slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    <span slot="title">IMU</span>
    <!-- <div class="flex flex-col">
        {#if $imu.x.length > 0}
            {#each Object.entries($imu) as [key, value]}
                <div>{key}: {value[value.length-1]}</div>
            {/each}
        {/if}
    </div> -->
    <div class="w-full overflow-x-auto">
		<div
			class="flex w-full flex-col space-y-1 h-60"
			transition:slide|local={{ duration: 300, easing: cubicOut }}
		>
			<canvas bind:this={heapChartElement} />
		</div>
	</div>
    <!-- <IMUSetting /> -->
</SettingsCard>