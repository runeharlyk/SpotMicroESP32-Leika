<script lang="ts">
    import { resolve } from '$app/paths'
    import { onDestroy, onMount } from 'svelte'
    import Visualization from '$lib/components/Visualization.svelte'
    import { notifications } from '$lib/components/toasts/notifications'
    import { Add, Bluetooth, Cancel, Check, Delete, Scan } from '$lib/components/icons'
    import { apiLocation, pairing, socket, startPairing } from '$lib/stores'
    import { addRobot, forgetRobot, markSeen, robots, subnetPrefix, type Robot } from '$lib/stores'
    import {
        normalizeSubnetPrefix,
        probeAddress,
        probeCandidates,
        sweepSubnet,
        type CandidateStatus
    } from '$lib/services/discovery'
    import { isBluetoothSupported } from '$lib/transport/ble-adapter'

    type Reachability = 'probing' | 'online' | 'offline'

    let adding = $state(false)
    let candidates = $state<CandidateStatus[]>([])
    let searching = $state(false)
    let sweeping = $state(false)
    let sweepProgress = $state({ done: 0, total: 0 })
    let sweepFound = $state<string[]>([])
    let prefixDraft = $state('')
    let manualAddress = $state('')
    let reachability = $state<Record<string, Reachability>>({})
    let controller: AbortController | undefined

    const unsaved = (address: string) => !$robots.some(robot => robot.address === address)

    const found = $derived(
        [...candidates.filter(c => c.state === 'found').map(c => c.address), ...sweepFound].filter(
            unsaved
        )
    )

    const anythingOnline = $derived(
        $socket || found.length > 0 || Object.values(reachability).includes('online')
    )

    const connectedName = $derived(
        $robots.find(robot => robot.address === $apiLocation)?.name ??
            ($socket && $apiLocation === '' ? 'this robot' : $apiLocation || 'the robot')
    )

    onMount(() => {
        prefixDraft = $subnetPrefix
        void search()
        void refreshSaved()
    })

    onDestroy(() => controller?.abort())

    const refreshSaved = async () => {
        for (const robot of $robots) reachability[robot.address] = 'probing'
        await Promise.all(
            $robots.map(async robot => {
                const isReachable = await probeAddress(robot.address)
                reachability[robot.address] = isReachable ? 'online' : 'offline'
                if (isReachable) markSeen(robot.address)
            })
        )
    }

    const search = async () => {
        controller?.abort()
        controller = new AbortController()
        searching = true
        try {
            await probeCandidates(statuses => (candidates = statuses), controller.signal)
        } finally {
            searching = false
        }
    }

    const startSweep = async () => {
        const prefix = normalizeSubnetPrefix(prefixDraft)
        if (!prefix) {
            notifications.error('Enter a subnet like 192.168.1', 4000)
            return
        }

        subnetPrefix.set(prefix)
        prefixDraft = prefix
        controller?.abort()
        controller = new AbortController()
        sweepFound = []
        sweeping = true
        sweepProgress = { done: 0, total: 0 }

        try {
            await sweepSubnet(prefix, {
                signal: controller.signal,
                onProgress: (done, total) => (sweepProgress = { done, total }),
                onFound: address => (sweepFound = [...sweepFound, address])
            })
        } finally {
            sweeping = false
        }
    }

    const stopSweep = () => {
        controller?.abort()
        sweeping = false
    }

    const connect = (address: string) => {
        apiLocation.set(address)
        socket.init(`ws://${address}/api/ws`)
    }

    const addAndConnect = (address: string) => {
        addRobot(address)
        connect(address)
    }

    const addManual = async () => {
        const address = manualAddress.trim()
        if (!address) return
        manualAddress = ''
        addRobot(address)
        reachability[address] = 'probing'
        reachability[address] = (await probeAddress(address)) ? 'online' : 'offline'
        adding = false
    }

    const pair = async () => {
        if (await startPairing()) adding = false
    }

    const statusLabel = (robot: Robot) => {
        const state = reachability[robot.address]
        if (state === 'probing') return 'Checking...'
        if (state === 'online') return 'Online'
        if (!robot.lastSeenAt) return 'Offline'
        const minutes = Math.round((Date.now() - robot.lastSeenAt) / 60000)
        if (minutes < 60) return `Offline - seen ${Math.max(1, minutes)} min ago`
        return `Offline - seen ${Math.round(minutes / 60)} h ago`
    }
</script>

<div
    class="flex h-[calc(100dvh-3rem)] w-full flex-col items-center justify-center p-4 lg:h-[calc(100dvh-4rem)]"
>
    <div class="w-full max-w-md">
        <div class="h-56 w-full sm:h-72">
            <Visualization
                defaultColor={null}
                orbit={anythingOnline}
                panel={false}
                ground={false}
            />
        </div>

        {#if $socket}
            <div class="bg-base-200 rounded-box p-4">
                <div class="flex items-center gap-2">
                    <span class="bg-success flex size-6 items-center justify-center rounded-full">
                        <Check class="text-base-100 h-4 w-4" />
                    </span>
                    <div class="min-w-0 flex-1">
                        <div class="truncate font-medium">Connected</div>
                        <div class="truncate text-xs opacity-60">{connectedName}</div>
                    </div>
                </div>

                <a class="btn btn-primary mt-4 w-full" href={resolve('/controller')}>
                    Open controller
                </a>
            </div>
        {:else if !adding}
            {#if $robots.length}
                <ul class="mb-4 flex flex-col gap-2">
                    {#each $robots as robot (robot.address)}
                        <li class="bg-base-200 rounded-box flex items-center gap-3 p-3">
                            {#if reachability[robot.address] === 'probing'}
                                <span class="loading loading-spinner loading-xs shrink-0"></span>
                            {:else}
                                <span
                                    class="size-2 shrink-0 rounded-full {(
                                        reachability[robot.address] === 'online'
                                    ) ?
                                        'bg-success'
                                    :   'bg-base-content/30'}"
                                ></span>
                            {/if}
                            <div class="min-w-0 flex-1">
                                <div class="truncate font-medium">{robot.name}</div>
                                <div class="truncate text-xs opacity-60">{statusLabel(robot)}</div>
                            </div>
                            <button
                                class="btn btn-sm btn-primary"
                                disabled={reachability[robot.address] === 'offline'}
                                onclick={() => connect(robot.address)}
                            >
                                Connect
                            </button>
                            <button
                                class="btn btn-sm btn-ghost btn-square"
                                aria-label="Forget {robot.name}"
                                onclick={() => forgetRobot(robot.address)}
                            >
                                <Delete class="h-4 w-4" />
                            </button>
                        </li>
                    {/each}
                </ul>
            {:else if found.length}
                <ul class="mb-4 flex flex-col gap-2">
                    {#each found as address (address)}
                        <li class="bg-base-200 rounded-box flex items-center gap-3 p-3">
                            <span class="bg-success size-2 shrink-0 rounded-full"></span>
                            <div class="min-w-0 flex-1">
                                <div class="truncate font-medium">Spot micro</div>
                                <div class="truncate font-mono text-xs opacity-60">{address}</div>
                            </div>
                            <button
                                class="btn btn-sm btn-primary"
                                onclick={() => addAndConnect(address)}
                            >
                                Add robot
                            </button>
                        </li>
                    {/each}
                </ul>
            {:else}
                <p class="mb-4 text-center text-sm opacity-60">
                    {#if searching}
                        Looking for a robot on this network...
                    {:else}
                        No robot found on this network yet.
                    {/if}
                </p>
            {/if}

            <button class="btn btn-primary w-full" onclick={() => (adding = true)}>
                <Add class="h-5 w-5" />
                Add robot
            </button>

            {#if !searching}
                <button class="btn btn-ghost btn-sm mt-2 w-full" onclick={search}>
                    <Scan class="h-4 w-4" />
                    Search again
                </button>
            {/if}
        {:else}
            <div class="mb-4 flex items-center justify-between gap-2">
                <h2 class="font-semibold">Add a robot</h2>
                <button class="btn btn-ghost btn-sm" onclick={() => (adding = false)}>
                    <Cancel class="h-4 w-4" />
                    Cancel
                </button>
            </div>

            {#if isBluetoothSupported()}
                <button class="btn btn-primary w-full" onclick={pair} disabled={$pairing}>
                    {#if $pairing}
                        <span class="loading loading-spinner loading-xs"></span>
                    {:else}
                        <Bluetooth class="h-5 w-5" />
                    {/if}
                    Connect over Bluetooth
                </button>
                <p class="mt-2 mb-4 text-xs opacity-60">
                    Easiest, with no network setup. Carries the controls and telemetry only; wifi
                    setup, file transfers and updates need a network address.
                </p>
            {:else}
                <p class="mb-4 text-xs opacity-60">
                    Bluetooth needs a secure page (https or localhost), so it is unavailable here.
                    Use a network address instead.
                </p>
            {/if}

            <div class="collapse-arrow bg-base-200 rounded-box collapse">
                <input type="checkbox" />
                <div class="collapse-title text-sm font-medium">Connect over WiFi instead</div>
                <div class="collapse-content flex flex-col gap-4">
                    <div>
                        <div class="mb-1 text-xs font-semibold opacity-70">Address</div>
                        <div class="flex gap-2">
                            <input
                                class="input input-sm min-w-0 flex-1"
                                aria-label="New robot address"
                                placeholder="hostname or IP"
                                bind:value={manualAddress}
                                onkeydown={e => e.key === 'Enter' && addManual()}
                            />
                            <button
                                class="btn btn-sm"
                                onclick={addManual}
                                disabled={!manualAddress.trim()}
                            >
                                Add
                            </button>
                        </div>
                    </div>

                    <div>
                        <div class="mb-1 text-xs font-semibold opacity-70">Sweep a subnet</div>
                        <div class="flex gap-2">
                            <input
                                class="input input-sm min-w-0 flex-1"
                                aria-label="Subnet prefix"
                                placeholder="192.168.1"
                                bind:value={prefixDraft}
                                disabled={sweeping}
                            />
                            {#if sweeping}
                                <button class="btn btn-sm" onclick={stopSweep}>Stop</button>
                            {:else}
                                <button class="btn btn-sm" onclick={startSweep}>Sweep</button>
                            {/if}
                        </div>
                        {#if sweeping || sweepProgress.done}
                            <div class="mt-2 flex items-center gap-2">
                                <progress
                                    class="progress progress-primary flex-1"
                                    value={sweepProgress.done}
                                    max={sweepProgress.total || 1}
                                ></progress>
                                <span class="font-mono text-xs tabular-nums">
                                    {sweepProgress.done}/{sweepProgress.total}
                                </span>
                            </div>
                        {/if}
                        {#each sweepFound.filter(unsaved) as address (address)}
                            <div class="mt-2 flex items-center gap-2">
                                <span class="bg-success size-2 shrink-0 rounded-full"></span>
                                <span class="min-w-0 flex-1 truncate font-mono text-xs">
                                    {address}
                                </span>
                                <button
                                    class="btn btn-xs btn-primary"
                                    onclick={() => addAndConnect(address)}
                                >
                                    Add
                                </button>
                            </div>
                        {/each}
                    </div>
                </div>
            </div>
        {/if}
    </div>
</div>
