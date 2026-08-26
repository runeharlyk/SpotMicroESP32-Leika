<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { Bluetooth, WiFi } from '$lib/components/icons'
    import { notifications } from '$lib/components/toasts/notifications'
    import { apiLocation, socket } from '$lib/stores'
    import { isBluetoothSupported } from '$lib/transport/ble-adapter'

    let connecting = $state(false)

    const update = () => {
        const ws = $apiLocation ? $apiLocation : window.location.host
        socket.init(`ws://${ws}/api/ws`)
    }

    // Web Bluetooth needs a user gesture and a secure context, so this only works from a click on
    // an HTTPS page. It is how the GitHub Pages build reaches a robot at all: a page served over
    // HTTPS cannot open a plain ws:// connection to the robot.
    const connectBluetooth = async () => {
        connecting = true
        try {
            await socket.connectBluetooth()
        } catch (error) {
            // Dismissing the browser's device chooser is a normal outcome, not a failure.
            if (!(error instanceof DOMException && error.name === 'NotFoundError')) {
                notifications.error(`Bluetooth connection failed: ${error}`, 5000)
            }
        } finally {
            connecting = false
        }
    }
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
        <button class="btn btn-primary" onclick={connectBluetooth} disabled={connecting}>
            {#if connecting}
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
