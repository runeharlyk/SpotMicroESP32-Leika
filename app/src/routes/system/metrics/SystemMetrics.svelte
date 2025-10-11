<script lang="ts">
    import { onMount } from 'svelte'
    import { page } from '$app/stores'
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { slide } from 'svelte/transition'
    import { cubicOut } from 'svelte/easing'
    import { Chart, registerables } from 'chart.js'

    import { daisyColor } from '$lib/utilities'
    import { analytics } from '$lib/stores/analytics'
    import { Metrics } from '$lib/components/icons'

    Chart.register(...registerables)

    let heapChartElement: HTMLCanvasElement
    let heapChart: Chart

    let filesystemChartElement: HTMLCanvasElement
    let filesystemChart: Chart

    let temperatureChartElement: HTMLCanvasElement
    let temperatureChart: Chart

    onMount(() => {
        heapChart = new Chart(heapChartElement, {
            type: 'line',
            data: {
                labels: $analytics.uptime,
                datasets: [
                    {
                        label: 'Used Heap',
                        borderColor: daisyColor('--color-primary'),
                        backgroundColor: daisyColor('--color-primary', 50),
                        borderWidth: 2,
                        data: $analytics.used_heap,
                        fill: true,
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
                            color: daisyColor('--color-base-content', 10)
                        },
                        ticks: {
                            color: daisyColor('--color-base-content')
                        },
                        display: false
                    },
                    y: {
                        type: 'linear',
                        title: {
                            display: true,
                            text: 'Heap [kb]',
                            color: daisyColor('--color-base-content'),
                            font: {
                                size: 16,
                                weight: 'bold'
                            }
                        },
                        position: 'left',
                        min: 0,
                        max: Math.round($analytics.total_heap[0]),
                        grid: { color: daisyColor('--color-base-content', 10) },
                        ticks: {
                            color: daisyColor('--color-base-content')
                        },
                        border: { color: daisyColor('--color-base-content', 10) }
                    }
                }
            }
        })
        filesystemChart = new Chart(filesystemChartElement, {
            type: 'line',
            data: {
                labels: $analytics.uptime,
                datasets: [
                    {
                        label: 'File System Used',
                        borderColor: daisyColor('--color-primary'),
                        backgroundColor: daisyColor('--color-primary', 50),
                        borderWidth: 2,
                        data: $analytics.fs_used,
                        fill: true,
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
                            color: daisyColor('--color-base-content', 10)
                        },
                        ticks: {
                            color: daisyColor('--color-base-content')
                        },
                        display: false
                    },
                    y: {
                        type: 'linear',
                        title: {
                            display: true,
                            text: 'File System [kb]',
                            color: daisyColor('--color-base-content'),
                            font: {
                                size: 16,
                                weight: 'bold'
                            }
                        },
                        position: 'left',
                        min: 0,
                        max: Math.round($analytics.fs_total[0]),
                        grid: { color: daisyColor('--color-base-content', 10) },
                        ticks: {
                            color: daisyColor('--color-base-content')
                        },
                        border: { color: daisyColor('--color-base-content', 10) }
                    }
                }
            }
        })
        temperatureChart = new Chart(temperatureChartElement, {
            type: 'line',
            data: {
                labels: $analytics.uptime,
                datasets: [
                    {
                        label: 'Core Temperature',
                        borderColor: daisyColor('--color-primary'),
                        backgroundColor: daisyColor('--color-primary', 50),
                        borderWidth: 2,
                        data: $analytics.core_temp,
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
                            color: daisyColor('--color-base-content', 10)
                        },
                        ticks: {
                            color: daisyColor('--color-base-content')
                        },
                        display: false
                    },
                    y: {
                        type: 'linear',
                        title: {
                            display: true,
                            text: 'Core Temperature [°C]',
                            color: daisyColor('--color-base-content'),
                            font: {
                                size: 16,
                                weight: 'bold'
                            }
                        },
                        position: 'left',
                        suggestedMin: 20,
                        suggestedMax: 100,
                        grid: { color: daisyColor('--color-base-content', 10) },
                        ticks: {
                            color: daisyColor('--color-base-content')
                        },
                        border: { color: daisyColor('--color-base-content', 10) }
                    }
                }
            }
        })
        setInterval(updateData, 500)
    })

    function updateData() {
        heapChart.data.labels = $analytics.uptime
        heapChart.data.datasets[0].data = $analytics.used_heap
        heapChart.options.scales!.y!.max = Math.ceil($analytics.total_heap[0])
        heapChart.update('none')

        filesystemChart.data.labels = $analytics.uptime
        filesystemChart.data.datasets[0].data = $analytics.fs_used
        heapChart.options.scales!.y!.max = Math.ceil($analytics.fs_total[0])
        filesystemChart.update('none')

        temperatureChart.data.labels = $analytics.uptime
        temperatureChart.data.datasets[0].data = $analytics.core_temp
        temperatureChart.update('none')
    }
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Metrics class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    {/snippet}
    {#snippet title()}
        <span>System Metrics</span>
    {/snippet}

    <div class="w-full overflow-x-auto">
        <div
            class="flex w-full flex-col space-y-1 h-60"
            transition:slide|local={{ duration: 300, easing: cubicOut }}
        >
            <canvas bind:this={heapChartElement}></canvas>
        </div>
    </div>
    <div class="w-full overflow-x-auto">
        <div
            class="flex w-full flex-col space-y-1 h-52"
            transition:slide|local={{ duration: 300, easing: cubicOut }}
        >
            <canvas bind:this={filesystemChartElement}></canvas>
        </div>
    </div>
    <div class="w-full overflow-x-auto">
        <div
            class="flex w-full flex-col space-y-1 h-52"
            transition:slide|local={{ duration: 300, easing: cubicOut }}
        >
            <canvas bind:this={temperatureChartElement}></canvas>
        </div>
    </div>
</SettingsCard>
