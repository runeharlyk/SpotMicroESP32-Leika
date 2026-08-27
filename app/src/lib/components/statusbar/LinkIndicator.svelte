<script lang="ts">
    import { apiLocation, pairing, socket, startPairing } from '$lib/stores'
    import { telemetry } from '$lib/stores/telemetry'
    import { isBluetoothSupported } from '$lib/transport/ble-adapter'
    import { Bluetooth, Connection } from '../icons'

    const transport = socket.transport
    const transportLabels = { websocket: 'WiFi', bluetooth: 'BLE' }

    // The link only counts as healthy once a pong has come back; a socket that is open but silent
    // still leaves the robot uncommanded.
    const responsive = $derived($telemetry.latency >= 0)

    const label = $derived(
        !$socket ? 'Offline' : (transportLabels[$transport ?? 'websocket'] ?? 'Online')
    )

    const dotClass = $derived(
        !$socket ? 'bg-error animate-pulse'
        : responsive ? 'bg-success'
        : 'bg-warning'
    )

    const detail = $derived(!$socket || !responsive ? '' : `${$telemetry.latency} ms`)

    const connectWifi = () => {
        const host = $apiLocation ? $apiLocation : window.location.host
        socket.init(`ws://${host}/api/ws`)
    }
</script>

<div class="dropdown dropdown-end">
    <div
        tabindex="0"
        role="button"
        class="btn btn-ghost btn-sm gap-2 px-2"
        aria-label="Robot link: {label} {detail}"
    >
        <span class="size-2 shrink-0 rounded-full {dotClass}"></span>
        <span class="text-xs font-medium">{label}</span>
        {#if detail}
            <span class="hidden font-mono text-xs opacity-60 sm:inline">{detail}</span>
        {/if}
    </div>

    <div class="dropdown-content bg-base-200 rounded-box z-50 mt-2 w-72 p-3 shadow-lg">
        <div class="flex items-center gap-2">
            <Connection class="h-5 w-5 shrink-0" />
            <span class="flex-1 text-sm font-semibold">WiFi</span>
            <span class="text-xs opacity-70">
                {$socket && $transport === 'websocket' ? 'Connected' : 'Disconnected'}
            </span>
        </div>

        <div class="mt-2 flex gap-2">
            <input
                class="input input-sm min-w-0 flex-1"
                aria-label="Robot address"
                placeholder={typeof window === 'undefined' ? '' : window.location.host}
                bind:value={$apiLocation}
            />
            <button class="btn btn-sm btn-primary" onclick={connectWifi}>Connect</button>
        </div>

        {#if isBluetoothSupported()}
            <div class="divider my-2"></div>

            <div class="flex items-center gap-2">
                <Bluetooth class="h-5 w-5 shrink-0" />
                <span class="flex-1 text-sm font-semibold">Bluetooth</span>
                <span class="text-xs opacity-70">
                    {$socket && $transport === 'bluetooth' ? 'Connected' : 'Disconnected'}
                </span>
            </div>

            <div class="mt-2 flex justify-end">
                <button class="btn btn-sm btn-primary" onclick={startPairing} disabled={$pairing}>
                    {#if $pairing}
                        <span class="loading loading-spinner loading-xs"></span>
                    {/if}
                    Pair
                </button>
            </div>
        {/if}
    </div>
</div>
