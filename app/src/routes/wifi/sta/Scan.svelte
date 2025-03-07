<script lang="ts">
    import { focusTrap } from 'svelte-focus-trap';
    import { fly } from 'svelte/transition';
    import { onMount, onDestroy } from 'svelte';
    import RssiIndicator from '$lib/components/statusbar/RSSIIndicator.svelte';
    import type { NetworkItem, NetworkList } from '$lib/types/models';
    import { api } from '$lib/api';
    import { AP, Network, Reload, Cancel } from '$lib/components/icons';
    import { modals, exitBeforeEnter } from 'svelte-modals';

    // provided by <Modals />
    interface Props {
        isOpen: boolean;
        storeNetwork: any;
    }

    let { isOpen, storeNetwork }: Props = $props();

    const encryptionType = [
        'Open',
        'WEP',
        'WPA PSK',
        'WPA2 PSK',
        'WPA WPA2 PSK',
        'WPA2 Enterprise',
        'WPA3 PSK',
        'WPA2 WPA3 PSK',
        'WAPI PSK'
    ];

    let listOfNetworks: NetworkItem[] = $state([]);

    let scanActive = $state(false);

    let pollingId: number;

    async function scanNetworks() {
        scanActive = true;
        await api.get('/api/wifi/scan');
        if ((await pollingResults()) == false) {
            pollingId = setInterval(() => pollingResults(), 1000);
        }
        return;
    }

    async function pollingResults() {
        const result = await api.get<NetworkList>('/api/wifi/networks');
        if (result.isErr()) {
            console.error(`Error occurred while fetching: `, result.inner);
            return false;
        }
        let response = result.inner;
        listOfNetworks = response.networks;
        scanActive = false;
        if (listOfNetworks.length) {
            clearInterval(pollingId);
            pollingId = 0;
        }
        return listOfNetworks.length;
    }

    onMount(() => {
        scanNetworks();
    });

    onDestroy(() => {
        if (pollingId) {
            clearInterval(pollingId);
            pollingId = 0;
        }
    });
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
                        {#each listOfNetworks as network, i}
                            <li>
                                <!-- svelte-ignore a11y_click_events_have_key_events -->
                                <div
                                    class="bg-base-200 rounded-btn my-1 flex items-center space-x-3 hover:scale-[1.02] active:scale-[0.98]"
                                    onclick={() => {
                                        storeNetwork(network.ssid);
                                    }}
                                    role="button"
                                    tabindex="0"
                                >
                                    <div class="mask mask-hexagon bg-primary h-auto w-10 shrink-0">
                                        <Network
                                            class="text-primary-content h-auto w-full scale-75"
                                        />
                                    </div>
                                    <div>
                                        <div class="font-bold">{network.ssid}</div>
                                        <div class="text-sm opacity-75">
                                            Security: {encryptionType[network.encryption_type]},
                                            Channel: {network.channel}
                                        </div>
                                    </div>
                                    <div class="grow"></div>
                                    <RssiIndicator showDBm={true} rssi={network.rssi} />
                                </div>
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
