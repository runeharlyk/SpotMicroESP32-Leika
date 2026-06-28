<script lang="ts">
    import { daisyColor } from '$lib/utilities'
    import { Chart } from '$lib/utilities/chart'
    import { onMount, onDestroy } from 'svelte'
    import { cubicOut } from 'svelte/easing'
    import { slide } from 'svelte/transition'

    let chartElement: HTMLCanvasElement
    let chart: Chart<'line', number[], number>
    let refreshIntervalId: ReturnType<typeof setInterval>

    interface Props {
        label: string
        data: number[]
        title: string
    }

    let { label, data, title }: Props = $props()

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
        })

        refreshIntervalId = setInterval(() => {
            chart.data.labels = data
            chart.data.datasets[0].data = data
            chart.update('none')
        }, 500)
    })

    onDestroy(() => {
        clearInterval(refreshIntervalId)
        chart?.destroy()
    })
</script>

<div class="w-full h-full overflow-x-auto">
    <div
        class="flex w-full flex-col space-y-1 h-60"
        transition:slide|local={{ duration: 300, easing: cubicOut }}
    >
        <canvas bind:this={chartElement}></canvas>
    </div>
</div>
