<script lang="ts">
	import SettingsCard from "$lib/components/SettingsCard.svelte";
    import Rotate3d from '~icons/mdi/rotate-3d';
    import { imu } from '$lib/stores/imu';
    import { Chart, registerables } from 'chart.js';
	import { cubicOut } from "svelte/easing";
	import { slide } from "svelte/transition";
	import { onDestroy, onMount } from "svelte";
	import { daisyColor } from "$lib/utilities";
	import { socket } from "$lib/stores";
	import type { IMU } from "$lib/types/models";
	import { useFeatureFlags } from "$lib/stores/featureFlags";

    const features = useFeatureFlags();

    Chart.register(...registerables);

    let angleChartElement: HTMLCanvasElement;
	let angleChart: Chart;

    let tempChartElement: HTMLCanvasElement;
	let tempChart: Chart;

    let altitudeChartElement: HTMLCanvasElement;
	let altitudeChart: Chart;

    const handleImu = (data: IMU) => {
        console.log(data);
        
        imu.addData(data);   
    }

    onMount(() => {
        socket.on('imu', handleImu);
        angleChart = new Chart(angleChartElement, {
			type: 'line',
			data: {
				datasets: [
					{
						label: 'x',
						borderColor: daisyColor('--p'),
						backgroundColor: daisyColor('--p', 50),
						borderWidth: 2,
						data: $imu.x,
						yAxisID: 'y'
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
						yAxisID: 'y'
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
        tempChart = new Chart(tempChartElement, {
			type: 'line',
			data: {
				datasets: [
					{
						label: 'Barometer temperature',
						borderColor: daisyColor('--s'),
						backgroundColor: daisyColor('--s', 50),
						borderWidth: 2,
						data: $imu.bmp_temp,
						yAxisID: 'y'
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
							text: 'Temperature [C°]',
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
        altitudeChart = new Chart(altitudeChartElement, {
			type: 'line',
			data: {
				datasets: [
					{
						label: 'Altitude',
						borderColor: daisyColor('--p'),
						backgroundColor: daisyColor('--p', 50),
						borderWidth: 2,
						data: $imu.altitude,
						yAxisID: 'y'
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
							text: 'Altitude [M]',
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
        if ($features.imu) {
            angleChart.data.labels = $imu.x;
            angleChart.data.datasets[0].data = $imu.x;
            angleChart.data.datasets[1].data = $imu.y;
            angleChart.data.datasets[2].data = $imu.z;
            angleChart.options.scales!.y!.min = Math.min(Math.min(...$imu.x), Math.min(...$imu.y), Math.min(...$imu.z)) - 1;
            angleChart.options.scales!.y!.max = Math.max(Math.max(...$imu.x), Math.max(...$imu.y), Math.max(...$imu.z)) + 1;
            angleChart.update('none');
        }

        if ($features.bmp) {
            tempChart.data.labels = $imu.bmp_temp;
            tempChart.data.datasets[0].data = $imu.bmp_temp;
            tempChart.options.scales!.y!.min = Math.min(...$imu.bmp_temp) - 1;
            tempChart.options.scales!.y!.max = Math.max(...$imu.bmp_temp) + 1;
            tempChart.update('none');
    
            altitudeChart.data.labels = $imu.altitude;
            altitudeChart.data.datasets[0].data = $imu.altitude;
            altitudeChart.options.scales!.y!.min = Math.min(Math.min(...$imu.altitude)) - 1;
            altitudeChart.options.scales!.y!.max = Math.max(Math.max(...$imu.altitude)) + 1;
            altitudeChart.update('none');
        }
    }

</script>

<SettingsCard collapsible={false}>
    <Rotate3d slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    <span slot="title">IMU</span>
    {#if $features.imu} 
        <div class="w-full overflow-x-auto">
            <div
            class="flex w-full flex-col space-y-1 h-60"
            transition:slide|local={{ duration: 300, easing: cubicOut }}
            >
                <canvas bind:this={angleChartElement} />
            </div>
        </div>
    {/if}
    {#if $features.bmp} 
        <div class="w-full overflow-x-auto">
            <div
                class="flex w-full flex-col space-y-1 h-60"
                transition:slide|local={{ duration: 300, easing: cubicOut }}
            >
                <canvas bind:this={tempChartElement} />
            </div>
        </div>
        <div class="w-full overflow-x-auto">
            <div
            class="flex w-full flex-col space-y-1 h-60"
            transition:slide|local={{ duration: 300, easing: cubicOut }}
            >
            <canvas bind:this={altitudeChartElement} />
        </div>
    </div>
    {/if}
    <!-- <IMUSetting /> -->
</SettingsCard>