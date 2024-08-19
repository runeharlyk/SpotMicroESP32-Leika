<script lang="ts">
	import { daisyColor } from "$lib/utilities";
	import { Chart, registerables } from "chart.js";
	import { onMount } from "svelte";
	import { cubicOut } from "svelte/easing";
	import { slide } from "svelte/transition";

    let chartElement: HTMLCanvasElement;
	let chart: Chart;

    export let label
    export let data:number[]
    export let title

    Chart.register(...registerables);

    onMount(() => {
        chart = new Chart(chartElement, {
			type: 'line',
			data: {
				labels: data,
				datasets: [
					{
						label,
						borderColor: daisyColor('--p'),
						backgroundColor: daisyColor('--p', 50),
						borderWidth: 2,
                        data,
						yAxisID: 'y'
					},
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
						radius: 0
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
							text: title,
							color: daisyColor('--bc'),
							font: {
								size: 16,
								weight: 'bold'
							}
						},
						position: 'left',
						min: 0,
						max: 100,
						grid: { color: daisyColor('--bc', 10) },
						ticks: {
							color: daisyColor('--bc')
						},
						border: { color: daisyColor('--bc', 10) }
					}
				}
			}
		});

        setInterval(() => {
            chart.data.labels = data
            chart.data.datasets[0].data = data
        }, 500);
    })
</script>


<div class="w-full h-full overflow-x-auto">
    <div
        class="flex w-full flex-col space-y-1 h-60"
        transition:slide|local={{ duration: 300, easing: cubicOut }}
    >
        <canvas bind:this={chartElement} />
    </div>
</div>