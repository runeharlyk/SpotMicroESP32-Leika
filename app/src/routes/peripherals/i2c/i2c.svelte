<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { onMount } from 'svelte'
    import { socket } from '$lib/stores'
    import { Connection } from '$lib/components/icons'
    import I2CSetting from './i2cSetting.svelte'
    import type { I2CDevice } from '$lib/platform_shared/message'

    let active_devices: I2CDevice[] = $state([])
    let isLoading = $state(false)

    onMount(() => {
        triggerScan()
    })

    const triggerScan = async () => {
        isLoading = true
        try {
            const response = await socket.request({ i2cScanDataRequest: {} })
            active_devices = response.i2cScanData?.devices ?? []
        } finally {
            isLoading = false
        }
    }
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Connection class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    {/snippet}
    {#snippet title()}
        <span>I<sup>2</sup>C</span>
    {/snippet}
    {#snippet right()}
        <button class="btn btn-primary" onclick={triggerScan} disabled={isLoading}>
            {#if isLoading}
                <span class="loading loading-ring loading-xs"></span>
            {:else}
                Scan
            {/if}
        </button>
    {/snippet}

    <I2CSetting />

    <div class="grid">
        {#if active_devices.length === 0}
            <div>No I2C devices found</div>
        {:else}
            {#each active_devices as device (device.address)}
                <div>[{device.address.toString(16)}] {device.partNumber} - {device.name}</div>
            {/each}
        {/if}
    </div>
</SettingsCard>
