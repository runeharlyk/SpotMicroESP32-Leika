<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { imu } from '$lib/stores/imu'
    import { Chart, registerables } from 'chart.js'
    import { cubicOut } from 'svelte/easing'
    import { slide } from 'svelte/transition'
    import { onDestroy, onMount } from 'svelte'
    import { socket } from '$lib/stores'
    import { MessageTopic, type IMUMsg, type IMUCalibrationResult } from '$lib/types/models'
    import { useFeatureFlags } from '$lib/stores/featureFlags'
    import { Rotate3d } from '$lib/components/icons'

    Chart.register(...registerables)

    const features = useFeatureFlags()
    let intervalId: ReturnType<typeof setInterval> | number
    let isCalibrating = $state(false)
    let calibrationResult = $state<IMUCalibrationResult | null>(null)

    let angleChartElement: HTMLCanvasElement
    let tempChartElement: HTMLCanvasElement
    let altitudeChartElement: HTMLCanvasElement

    let angleChart: Chart
    let tempChart: Chart
    let altitudeChart: Chart

    const getChartColors = () => {
        const style = getComputedStyle(document.body)
        return {
            primary: style.getPropertyValue('--color-primary'),
            secondary: style.getPropertyValue('--color-secondary'),
            accent: style.getPropertyValue('--color-accent'),
            background: style.getPropertyValue('--color-background')
        }
    }

    const createBaseChartConfig = (bgColor: string) => ({
        maintainAspectRatio: false,
        responsive: true,
        plugins: {
            legend: { display: true },
            tooltip: { mode: 'index' as const, intersect: false }
        },
        elements: { point: { radius: 1 } },
        scales: {
            x: {
                grid: { color: bgColor },
                ticks: { color: bgColor },
                display: false
            },
            y: {
                type: 'linear' as const,
                position: 'left' as const,
                min: 0,
                max: 10,
                grid: { color: bgColor },
                ticks: { color: bgColor },
                border: { color: bgColor }
            }
        }
    })

    const initializeCharts = () => {
        const colors = getChartColors()
        const baseConfig = createBaseChartConfig(colors.background)

        angleChart = new Chart(angleChartElement, {
            type: 'line',
            data: {
                datasets: [
                    {
                        label: 'x',
                        borderColor: colors.primary,
                        backgroundColor: colors.primary,
                        borderWidth: 2,
                        data: $imu.x,
                        yAxisID: 'y'
                    },
                    {
                        label: 'y',
                        borderColor: colors.secondary,
                        backgroundColor: colors.secondary,
                        borderWidth: 2,
                        data: $imu.y,
                        yAxisID: 'y'
                    },
                    {
                        label: 'z',
                        borderColor: colors.accent,
                        backgroundColor: colors.accent,
                        borderWidth: 2,
                        data: $imu.z,
                        yAxisID: 'y'
                    }
                ]
            },
            options: {
                ...baseConfig,
                scales: {
                    ...baseConfig.scales,
                    y: {
                        ...baseConfig.scales.y,
                        title: {
                            display: true,
                            text: 'Angle [°]',
                            color: colors.background,
                            font: { size: 16, weight: 'bold' }
                        }
                    }
                }
            }
        })

        tempChart = new Chart(tempChartElement, {
            type: 'line',
            data: {
                datasets: [
                    {
                        label: 'Barometer temperature',
                        borderColor: colors.secondary,
                        backgroundColor: colors.secondary,
                        borderWidth: 2,
                        data: $imu.bmp_temp,
                        yAxisID: 'y'
                    }
                ]
            },
            options: {
                ...baseConfig,
                scales: {
                    ...baseConfig.scales,
                    y: {
                        ...baseConfig.scales.y,
                        title: {
                            display: true,
                            text: 'Temperature [C°]',
                            color: colors.background,
                            font: { size: 16, weight: 'bold' }
                        }
                    }
                }
            }
        })

        altitudeChart = new Chart(altitudeChartElement, {
            type: 'line',
            data: {
                datasets: [
                    {
                        label: 'Altitude',
                        borderColor: colors.primary,
                        backgroundColor: colors.primary,
                        borderWidth: 2,
                        data: $imu.altitude,
                        yAxisID: 'y'
                    }
                ]
            },
            options: {
                ...baseConfig,
                scales: {
                    ...baseConfig.scales,
                    y: {
                        ...baseConfig.scales.y,
                        title: {
                            display: true,
                            text: 'Altitude [M]',
                            color: colors.background,
                            font: { size: 16, weight: 'bold' }
                        }
                    }
                }
            }
        })
    }

    const updateChartData = (chart: Chart, data: number[]) => {
        chart.data.labels = data
        chart.data.datasets[0].data = data
        chart.options.scales!.y!.min = Math.min(...data) - 1
        chart.options.scales!.y!.max = Math.max(...data) + 1
        chart.update('none')
    }

    const updateData = () => {
        if ($features.imu) {
            angleChart.data.labels = $imu.x
            angleChart.data.datasets[0].data = $imu.x
            angleChart.data.datasets[1].data = $imu.y
            angleChart.data.datasets[2].data = $imu.z

            const allValues = [...$imu.x, ...$imu.y, ...$imu.z]
            angleChart.options.scales!.y!.min = Math.min(...allValues) - 1
            angleChart.options.scales!.y!.max = Math.max(...allValues) + 1
            angleChart.update('none')
        }

        if ($features.bmp) {
            updateChartData(tempChart, $imu.bmp_temp)
            updateChartData(altitudeChart, $imu.altitude)
        }
    }

    onMount(() => {
        socket.on(MessageTopic.imu, (data: IMUMsg) => {
            console.log(data)
            imu.addData(data)
        })

        socket.on(MessageTopic.imuCalibrate, (data: IMUCalibrationResult) => {
            isCalibrating = false
            calibrationResult = data
        })

        initializeCharts()
        intervalId = setInterval(updateData, 200)
    })

    onDestroy(() => {
        socket.off(MessageTopic.imu)
        socket.off(MessageTopic.imuCalibrate)
        clearInterval(intervalId)
    })

    function startCalibration() {
        isCalibrating = true
        calibrationResult = null
        socket.sendEvent(MessageTopic.imuCalibrate, {})
    }
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Rotate3d class="flex-shrink-0 mr-2 h-6 w-6 self-end" />
    {/snippet}
    {#snippet title()}
        <span>IMU</span>
    {/snippet}

    <div class="flex items-center gap-2 mb-4">
        <button
            class="btn btn-sm btn-primary"
            onclick={startCalibration}
            disabled={isCalibrating || !$features.imu}
        >
            {#if isCalibrating}
                <span class="loading loading-spinner loading-xs"></span>
                Calibrating...
            {:else}
                Calibrate IMU
            {/if}
        </button>
        {#if calibrationResult}
            <span class="badge" class:badge-success={calibrationResult.success} class:badge-error={!calibrationResult.success}>
                {calibrationResult.success ? 'Calibrated' : 'Failed'}
            </span>
        {/if}
    </div>

    {#if $features.imu}
        <div class="w-full overflow-x-auto">
            <div
                class="flex w-full flex-col space-y-1 h-60"
                transition:slide|local={{ duration: 300, easing: cubicOut }}
            >
                <canvas bind:this={angleChartElement}></canvas>
            </div>
        </div>
    {/if}

    {#if $features.bmp}
        <div class="w-full overflow-x-auto">
            <div
                class="flex w-full flex-col space-y-1 h-60"
                transition:slide|local={{ duration: 300, easing: cubicOut }}
            >
                <canvas bind:this={tempChartElement}></canvas>
            </div>
        </div>
        <div class="w-full overflow-x-auto">
            <div
                class="flex w-full flex-col space-y-1 h-60"
                transition:slide|local={{ duration: 300, easing: cubicOut }}
            >
                <canvas bind:this={altitudeChartElement}></canvas>
            </div>
        </div>
    {/if}
</SettingsCard>
