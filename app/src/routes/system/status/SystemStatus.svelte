<script lang="ts">
    import { onDestroy, onMount } from 'svelte'
    import type { Component } from 'svelte'
    import { modals } from 'svelte-modals'
    import ConfirmDialog from '$lib/components/ConfirmDialog.svelte'
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import Spinner from '$lib/components/Spinner.svelte'
    import { slide } from 'svelte/transition'
    import { cubicOut } from 'svelte/easing'
    import { socket } from '$lib/stores/socket'
    import { api } from '$lib/api'
    import { convertSeconds } from '$lib/utilities'
    import { useFeatureFlags } from '$lib/stores/featureFlags'
    import {
        Cancel,
        Power,
        FactoryReset,
        Sleep,
        Health,
        CPU,
        SDK,
        CPP,
        Speed,
        Heap,
        Pyramid,
        Sketch,
        Flash,
        Folder,
        Temperature,
        Stopwatch
    } from '$lib/components/icons'
    import StatusItem from '$lib/components/StatusItem.svelte'
    import ActionButton from './ActionButton.svelte'
    import { AnalyticsData, type SystemInformation } from '$lib/platform_shared/message'

    const features = useFeatureFlags()

    let systemInformation: SystemInformation | null = $state(null)

    async function getSystemStatus() {
        const result = await api.get<SystemInformation>('/api/system/status')
        if (result.isErr()) {
            console.error('Error:', result.inner)
            return
        }
        systemInformation = result.inner
        return systemInformation
    }

    const postFactoryReset = async () => await api.post('/api/system/reset')

    const postSleep = async () => await api.post('api/sleep')

    let unsub: (() => void) | undefined = undefined
    onMount(() => (unsub = socket.on(AnalyticsData, handleSystemData)))
    onDestroy(() => {
        if (unsub) unsub()
    })

    const handleSystemData = (data: AnalyticsData) => {
        if (systemInformation) {
            systemInformation = {
                ...systemInformation,
                ...(data as unknown as SystemInformation)
            }
        }
    }

    const postRestart = async () => await api.post('/api/system/restart')

    function confirmRestart() {
        modals.open(ConfirmDialog, {
            title: 'Confirm Restart',
            message: 'Are you sure you want to restart the device?',
            labels: {
                cancel: { label: 'Abort', icon: Cancel },
                confirm: { label: 'Restart', icon: Power }
            },
            onConfirm: () => {
                modals.close()
                postRestart()
            }
        })
    }

    function confirmReset() {
        modals.open(ConfirmDialog, {
            title: 'Confirm Factory Reset',
            message: 'Are you sure you want to reset the device to its factory defaults?',
            labels: {
                cancel: { label: 'Abort', icon: Cancel },
                confirm: { label: 'Factory Reset', icon: FactoryReset }
            },
            onConfirm: () => {
                modals.close()
                postFactoryReset()
            }
        })
    }

    function confirmSleep() {
        modals.open(ConfirmDialog, {
            title: 'Confirm Going to Sleep',
            message: 'Are you sure you want to put the device into sleep?',
            labels: {
                cancel: { label: 'Abort', icon: Cancel },
                confirm: { label: 'Sleep', icon: Sleep }
            },
            onConfirm: () => {
                modals.close()
                postSleep()
            }
        })
    }

    interface ActionButtonDef {
        icon: Component
        label: string
        onClick: () => void
        type?: string
        condition?: () => boolean
    }

    const actionButtons: ActionButtonDef[] = [
        {
            icon: Sleep,
            label: 'Sleep',
            onClick: confirmSleep,
            condition: () => Boolean($features.sleep)
        },
        {
            icon: Power,
            label: 'Restart',
            onClick: confirmRestart
        },
        {
            icon: FactoryReset,
            label: 'Factory Reset',
            onClick: confirmReset,
            type: 'secondary'
        }
    ]
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Health class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    {/snippet}
    {#snippet title()}
        <span>System Status</span>
    {/snippet}

    <div class="w-full overflow-x-auto">
        {#await getSystemStatus()}
            <Spinner />
        {:then}
            {#if systemInformation}
                <div
                    class="flex w-full flex-col space-y-1"
                    transition:slide|local={{ duration: 300, easing: cubicOut }}
                >
                    <StatusItem
                        icon={CPU}
                        title="Chip"
                        description={`${systemInformation.staticSystemInformation?.cpuType} Rev ${systemInformation.staticSystemInformation?.cpuRev}`}
                    />

                    <StatusItem
                        icon={SDK}
                        title="SDK Version"
                        description={`ESP-IDF ${systemInformation.staticSystemInformation?.sdkVersion} / Arduino ${systemInformation.staticSystemInformation?.arduinoVersion}`}
                    />

                    <StatusItem
                        icon={CPP}
                        title="Firmware Version"
                        description={systemInformation.staticSystemInformation?.firmwareVersion}
                    />

                    <StatusItem
                        icon={Speed}
                        title="CPU Frequency"
                        description={`${systemInformation.staticSystemInformation?.cpuFreqMhz} MHz ${
                            systemInformation.staticSystemInformation?.cpuCores == 2 ?
                                'Dual Core'
                            :   'Single Core'
                        }`}
                    />

                    <StatusItem
                        icon={Heap}
                        title="Heap (Free / Max Alloc)"
                        description={`${systemInformation.analyticsData?.freeHeap} / ${systemInformation.analyticsData?.maxAllocHeap} bytes`}
                    />

                    <StatusItem
                        icon={Pyramid}
                        title="PSRAM (Size / Free)"
                        description={`${systemInformation.analyticsData!.psramSize - systemInformation.analyticsData!.freePsram} / ${systemInformation.analyticsData?.psramSize} bytes`}
                    />

                    <StatusItem
                        icon={Sketch}
                        title="Sketch (Used / Free)"
                        description={`${(
                            (systemInformation.staticSystemInformation!.sketchSize /
                                systemInformation.staticSystemInformation!.freeSketchSpace) *
                            100
                        ).toFixed(1)} % of
                ${systemInformation.staticSystemInformation!.freeSketchSpace / 1000000} MB used (${
                    (systemInformation.staticSystemInformation!.freeSketchSpace -
                        systemInformation.staticSystemInformation!.sketchSize) /
                    1000000
                } MB free)`}
                    />

                    <StatusItem
                        icon={Flash}
                        title="Flash Chip (Size / Speed)"
                        description={`${systemInformation.staticSystemInformation!.flashChipSize / 1000000} MB / ${
                            systemInformation.staticSystemInformation!.flashChipSpeed / 1000000
                        } MHz`}
                    />

                    <StatusItem
                        icon={Folder}
                        title="File System (Used / Total)"
                        description={`${(
                            (systemInformation.analyticsData!.fsUsed /
                                systemInformation.analyticsData!.fsTotal) *
                            100
                        ).toFixed(
                            1
                        )} % of ${systemInformation.analyticsData!.fsTotal / 1000000} MB used (${
                            (systemInformation.analyticsData!.fsTotal -
                                systemInformation.analyticsData!.fsUsed) /
                            1000000
                        }
                MB free)`}
                    />

                    <StatusItem
                        icon={Temperature}
                        title="Core Temperature"
                        description={`${
                            systemInformation.analyticsData!.coreTemp == 53.33 ?
                                'NaN'
                            :   systemInformation.analyticsData!.coreTemp.toFixed(2) + ' °C'
                        }`}
                    />

                    <StatusItem
                        icon={Stopwatch}
                        title="Uptime"
                        description={convertSeconds(systemInformation.analyticsData!.uptime)}
                    />

                    <StatusItem
                        icon={Power}
                        title="Reset Reason"
                        description={systemInformation.staticSystemInformation?.cpuResetReason}
                    />
                </div>
            {/if}
        {/await}
    </div>

    <div class="mt-4 flex flex-wrap justify-end gap-2">
        {#each actionButtons as button (button.label)}
            {#if button.condition === undefined || button.condition()}
                <ActionButton
                    onclick={button.onClick}
                    icon={button.icon}
                    label={button.label}
                    type={button.type || 'primary'}
                />
            {/if}
        {/each}
    </div>
</SettingsCard>
