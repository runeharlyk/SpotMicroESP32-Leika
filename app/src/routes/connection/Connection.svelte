<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { Bluetooth, WiFi } from '$lib/components/icons'
    import { apiLocation, pairing, robotSocketUrl, socket, startPairing } from '$lib/stores'
    import { isBluetoothSupported } from '$lib/transport/ble-adapter'

    const update = () => socket.init(robotSocketUrl())
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <WiFi class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    {/snippet}
    {#snippet title()}
        <span>Connection</span>
    {/snippet}

    <div class="flex">
        <label class="label w-32" for="server">Address:</label>
        <input class="input" bind:value={$apiLocation} />
    </div>

    <button class="btn btn-primary" onclick={update}>Update</button>
</SettingsCard>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Bluetooth class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    {/snippet}
    {#snippet title()}
        <span>Bluetooth</span>
    {/snippet}

    {#if isBluetoothSupported()}
        <p class="text-base-content/70 text-sm">
            Drive the robot without joining its network. Bluetooth carries the controls and
            telemetry only; wifi setup, file transfers and updates need the address above.
        </p>
        <button class="btn btn-primary" onclick={startPairing} disabled={$pairing}>
            {#if $pairing}
                <span class="loading loading-spinner loading-xs"></span>
            {/if}
            Pair a robot
        </button>
    {:else}
        <p class="text-base-content/70 text-sm">
            This browser has no Web Bluetooth support. It needs Chrome or Edge, on a page served
            over HTTPS.
        </p>
    {/if}
</SettingsCard>
