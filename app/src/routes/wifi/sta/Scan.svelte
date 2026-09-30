<script lang="ts">
    import { focusTrap } from 'svelte-focus-trap'
    import { fly } from 'svelte/transition'
    import { onMount, onDestroy } from 'svelte'
    import RssiIndicator from '$lib/components/statusbar/RSSIIndicator.svelte'
    import type { WifiNetworkScan } from '$lib/platform_shared/api'
    import { robotRequest } from '$lib/robot-request'
    import { AP, Network, Reload, Cancel } from '$lib/components/icons'
    import { modals, exitBeforeEnter, type ModalProps } from 'svelte-modals'

    let { isOpen, storeNetwork }: ModalProps = $props()

    const encryptionTypes = [
        'Open',
        'WEP',
        'WPA PSK',
        'WPA2 PSK',
        'WPA WPA2 PSK',
        'WPA2 Enterprise',
        'WPA3 PSK',
        'WPA2 WPA3 PSK',
        'WAPI PSK'
    ]

    let listOfNetworks = $state<WifiNetworkScan[]>([])

    let scanActive = $state(false)

    let pollingId: ReturnType<typeof setInterval> | undefined
    // A scan answered after the dialog closed must not start polling.
    let closed = false

    const stopPolling = () => {
        clearInterval(pollingId)
        pollingId = undefined
    }

    async function scanNetworks() {
        scanActive = true
        try {
            await robotRequest({ wifiScanStart: {} })
        } catch (error) {
            console.error('Starting a Wi-Fi scan failed: ', error)
        }
        if (closed || (await pollResults())) return
        stopPolling()
        pollingId = setInterval(pollResults, 1000)
    }

    /** True once the robot has finished scanning and the list is shown. */
    async function pollResults() {
        try {
            const reply = await robotRequest({ wifiNetworksRequest: {} })
            // 202: still scanning
            if (reply.statusCode === 202 || !reply.wifiNetworkList) return false
            listOfNetworks = reply.wifiNetworkList.networks
            scanActive = false
            stopPolling()
            return true
        } catch (error) {
            console.error('Fetching Wi-Fi scan results failed: ', error)
            return false
        }
    }

    onMount(() => {
        scanNetworks()
    })

    onDestroy(() => {
        closed = true
        stopPolling()
    })
</script>

{#if isOpen}
    <div
        role="dialog"
        class="pointer-events-none fixed inset-0 z-50 flex items-center justify-center"
        transition:fly={{ y: 50 }}
        use:exitBeforeEnter
        use:focusTrap
    >
        <div
            class="bg-base-100 rounded-box pointer-events-auto flex max-h-full min-w-fit max-w-md flex-col justify-between p-4 shadow-lg"
        >
            <h2 class="text-base-content text-start text-2xl font-bold">Scan Networks</h2>
            <div class="divider my-2"></div>
            <div class="overflow-y-auto">
                {#if scanActive}<div
                        class="bg-base-100 flex flex-col items-center justify-center p-6"
                    >
                        <AP class="text-secondary h-32 w-32 shrink animate-ping stroke-2" />
                        <p class="mt-8 text-2xl">Scanning ...</p>
                    </div>
                {:else}
                    <ul class="menu">
                        {#each listOfNetworks as network (network.ssid)}
                            <li>
                                <button
                                    type="button"
                                    class="bg-base-200 rounded-btn my-1 flex items-center space-x-3 text-left hover:scale-[1.02] active:scale-[0.98]"
                                    onclick={() => {
                                        storeNetwork(network.ssid)
                                    }}
                                >
                                    <div class="mask mask-hexagon bg-primary h-auto w-10 shrink-0">
                                        <Network
                                            class="text-primary-content h-auto w-full scale-75"
                                        />
                                    </div>
                                    <div>
                                        <div class="font-bold">{network.ssid}</div>
                                        <div class="text-sm opacity-75">
                                            Security: {encryptionTypes[network.encryptionType]},
                                            Channel: {network.channel}
                                        </div>
                                    </div>
                                    <div class="grow"></div>
                                    <RssiIndicator showDBm={true} rssi={network.rssi} />
                                </button>
                            </li>
                        {/each}
                    </ul>
                {/if}
            </div>
            <div class="divider my-2"></div>
            <div class="flex flex-wrap justify-end gap-2">
                <button
                    class="btn btn-primary inline-flex flex-none items-center"
                    disabled={scanActive}
                    onclick={scanNetworks}
                >
                    <Reload class="mr-2 h-5 w-5" /><span>Scan again</span>
                </button>

                <div class="grow"></div>
                <button
                    class="btn btn-warning text-warning-content inline-flex flex-none items-center"
                    onclick={() => modals.close()}
                >
                    <Cancel class="mr-2 h-5 w-5" /><span>Cancel</span>
                </button>
            </div>
        </div>
    </div>
{/if}
