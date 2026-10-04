<script lang="ts">
    import { modals } from 'svelte-modals'
    import { slide } from 'svelte/transition'
    import { cubicOut } from 'svelte/easing'
    import { notifications } from '$lib/components/toasts/notifications'
    import DragDropList, { VerticalDropZone, reorder, type DropEvent } from 'svelte-dnd-list'
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { PasswordInput, TextField } from '$lib/components/input'
    import ConfirmDialog from '$lib/components/ConfirmDialog.svelte'
    import ScanNetworks from './Scan.svelte'
    import Spinner from '$lib/components/Spinner.svelte'
    import LoadError from '$lib/components/LoadError.svelte'
    import InfoDialog from '$lib/components/InfoDialog.svelte'
    import { WifiNetwork, WifiStatus, type WifiSettings } from '$lib/platform_shared/api'
    import { socket } from '$lib/stores'
    import { robotRequest } from '$lib/robot-request'
    import { uint32ToIp } from '$lib/utilities'
    import {
        draftFromNetwork,
        hostnameError,
        networkErrors,
        networkFromDraft,
        type NetworkDraft,
        type NetworkErrors
    } from '$lib/network-settings'
    import {
        Cancel,
        Delete,
        Check,
        Router,
        AP,
        SSID,
        Home,
        WiFi,
        Down,
        MAC,
        Channel,
        Gateway,
        Subnet,
        DNS,
        Add,
        Scan,
        Edit
    } from '$lib/components/icons'
    import StatusItem from '$lib/components/StatusItem.svelte'

    // The robot stores at most this many networks (api.options).
    const MAX_NETWORKS = 5

    let wifiStatus: WifiStatus | null = $state(null)
    // The settings as the robot holds them; the page changes them only through a save the robot accepts.
    let settings: WifiSettings | null = $state(null)
    let general = $state({ hostname: '', priorityRssi: false })
    let hostnameProblem = $state<string>()

    // The network being added or edited, apart from the saved list until it is saved.
    // The draft outlives the open editor: a closing form's inputs still read it for a tick.
    let editorOpen = $state(false)
    let draft = $state<NetworkDraft>(draftFromNetwork(WifiNetwork.create()))
    let editing = $state<WifiNetwork | undefined>()
    let editorErrors = $state<NetworkErrors>({})

    let showWifiDetails = $state(false)

    async function getWifiStatus() {
        const reply = await robotRequest({ wifiStatusRequest: {} })
        if (!reply.wifiStatus) throw new Error('The robot sent no Wi-Fi status')
        wifiStatus = reply.wifiStatus
        return wifiStatus
    }

    async function getWifiSettings() {
        const reply = await robotRequest({ wifiSettingsRequest: {} })
        if (!reply.wifiSettings) throw new Error('The robot sent no Wi-Fi settings')
        settings = reply.wifiSettings
        general = { hostname: settings.hostname, priorityRssi: settings.priorityRssi }
        return settings
    }

    let statusLoad = $state(getWifiStatus())

    // The robot reports every change of the station, which a page opened over USB sees it join the network by.
    $effect(() => socket.on(WifiStatus, status => (wifiStatus = status)))
    let settingsLoad = $state(getWifiSettings())

    /** Saves `changes` on top of the stored settings; true once the robot accepted them. */
    async function save(changes: Partial<WifiSettings>) {
        if (!settings) return false
        const next = { ...$state.snapshot(settings), ...$state.snapshot(changes) }
        try {
            const reply = await robotRequest({ wifiSettings: next })
            settings = reply.wifiSettings ?? next
            notifications.success('Wi-Fi settings updated.', 3000)
            return true
        } catch (error) {
            notifications.error(`Saving Wi-Fi settings failed: ${(error as Error).message}`, 5000)
            return false
        }
    }

    function applyGeneral() {
        hostnameProblem = hostnameError(general.hostname)
        if (!hostnameProblem) save(general)
    }

    function openEditor(network: WifiNetwork, replacing?: WifiNetwork) {
        draft = draftFromNetwork(network)
        editing = replacing
        editorErrors = {}
        editorOpen = true
    }

    function roomForAnother() {
        if ((settings?.wifiNetworks.length ?? 0) < MAX_NETWORKS) return true
        modals.open(InfoDialog, {
            title: 'Reached Maximum Networks',
            message: `The robot keeps up to ${MAX_NETWORKS} networks. Delete one to add another.`,
            dismiss: { label: 'OK', icon: Check },
            onDismiss: () => modals.close()
        })
        return false
    }

    function scanForNetworks() {
        modals.open(ScanNetworks, {
            storeNetwork: (ssid: string) => {
                openEditor(WifiNetwork.create({ ssid }))
                modals.close()
            }
        })
    }

    async function saveNetwork(event: SubmitEvent) {
        event.preventDefault()
        if (!editorOpen || !settings) return
        editorErrors = networkErrors(draft, settings.wifiNetworks, editing)
        if (Object.keys(editorErrors).length) return
        const network = networkFromDraft(draft)
        const wifiNetworks =
            editing ?
                settings.wifiNetworks.map(saved => (saved === editing ? network : saved))
            :   [...settings.wifiNetworks, network]
        if (await save({ wifiNetworks })) editorOpen = false
    }

    function confirmDelete(index: number) {
        modals.open(ConfirmDialog, {
            title: 'Delete Network',
            message: 'Are you sure you want to delete this network?',
            labels: {
                cancel: { label: 'Cancel', icon: Cancel },
                confirm: { label: 'Delete', icon: Delete }
            },
            onConfirm: async () => {
                modals.close()
                if (!settings) return
                const deleted = settings.wifiNetworks[index]
                const saved = await save({
                    wifiNetworks: settings.wifiNetworks.filter((_, i) => i !== index)
                })
                if (saved && editorOpen && editing === deleted) editorOpen = false
            }
        })
    }

    function onDrop({ detail: { from, to } }: CustomEvent<DropEvent>) {
        if (!settings || !to || from === to) return
        save({ wifiNetworks: reorder(settings.wifiNetworks, from.index, to.index) })
    }
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Router class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    {/snippet}
    {#snippet title()}
        <span>WiFi Connection</span>
    {/snippet}
    <div class="w-full overflow-x-auto">
        {#await statusLoad}
            <Spinner />
        {:then}
            {#if wifiStatus}
                <div
                    class="flex w-full flex-col space-y-1"
                    transition:slide|local={{ duration: 300, easing: cubicOut }}
                >
                    <StatusItem
                        icon={AP}
                        title="Status"
                        variant={wifiStatus.status === 3 ? 'success' : 'error'}
                        description={wifiStatus.status === 3 ? 'Connected' : 'Inactive'}
                    />

                    {#if wifiStatus.status === 3}
                        <StatusItem icon={SSID} title="SSID" description={wifiStatus.ssid} />

                        <StatusItem
                            icon={Home}
                            title="IP Address"
                            description={uint32ToIp(wifiStatus.localIp)}
                        />

                        <StatusItem icon={WiFi} title="RSSI" description={`${wifiStatus.rssi} dBm`}>
                            <button
                                aria-label="Toggle connection details"
                                class="btn btn-circle btn-ghost btn-sm modal-button"
                                onclick={() => {
                                    showWifiDetails = !showWifiDetails
                                }}
                            >
                                <Down
                                    class="text-base-content h-auto w-6 transition-transform duration-300 ease-in-out {(
                                        showWifiDetails
                                    ) ?
                                        'rotate-180'
                                    :   ''}"
                                />
                            </button>
                        </StatusItem>
                    {/if}
                </div>

                <!-- Folds open -->
                {#if showWifiDetails}
                    <div
                        class="flex w-full flex-col space-y-1 pt-1"
                        transition:slide|local={{ duration: 300, easing: cubicOut }}
                    >
                        <StatusItem
                            icon={MAC}
                            title="MAC Address"
                            description={wifiStatus.macAddress}
                        />

                        <StatusItem
                            icon={Channel}
                            title="Channel"
                            description={wifiStatus.channel}
                        />

                        <StatusItem
                            icon={Gateway}
                            title="Gateway IP"
                            description={uint32ToIp(wifiStatus.gatewayIp)}
                        />

                        <StatusItem
                            icon={Subnet}
                            title="Subnet Mask"
                            description={uint32ToIp(wifiStatus.subnetMask)}
                        />

                        <StatusItem
                            icon={DNS}
                            title="DNS"
                            description={uint32ToIp(wifiStatus.dnsIp1)}
                        />
                    </div>
                {/if}
            {/if}
        {:catch error}
            <LoadError {error} retry={() => (statusLoad = getWifiStatus())} />
        {/await}
    </div>

    <div class="bg-base-200 relative grid w-full max-w-2xl self-center overflow-hidden">
        <div
            class="min-h-16 flex w-full items-center justify-between space-x-3 p-0 text-xl font-medium"
        >
            Saved Networks
        </div>
        {#await settingsLoad}
            <Spinner />
        {:then}
            {#if settings}
                <div class="relative w-full overflow-visible">
                    <button
                        aria-label="Add network"
                        class="btn btn-primary text-primary-content btn-md absolute -top-14 right-16"
                        onclick={() => roomForAnother() && openEditor(WifiNetwork.create())}
                    >
                        <Add class="h-6 w-6" /></button
                    >
                    <button
                        aria-label="Scan for networks"
                        class="btn btn-primary text-primary-content btn-md absolute -top-14 right-0"
                        onclick={() => roomForAnother() && scanForNetworks()}
                    >
                        <Scan class="h-6 w-6" /></button
                    >

                    <div
                        class="overflow-x-auto space-y-1"
                        transition:slide|local={{ duration: 300, easing: cubicOut }}
                    >
                        <DragDropList
                            id="networks"
                            type={VerticalDropZone}
                            itemSize={60}
                            itemCount={settings.wifiNetworks.length}
                            on:drop={onDrop}
                        >
                            {#snippet children({ index }: { index: number })}
                                {@const network = settings!.wifiNetworks[index]}
                                <StatusItem icon={Router} title={network.ssid}>
                                    <div class="space-x-0 px-0 mx-0">
                                        <button
                                            aria-label="Edit network"
                                            class="btn btn-ghost btn-sm"
                                            onclick={() => openEditor(network, network)}
                                        >
                                            <Edit class="h-6 w-6" /></button
                                        >
                                        <button
                                            aria-label="Delete network"
                                            class="btn btn-ghost btn-sm"
                                            onclick={() => confirmDelete(index)}
                                        >
                                            <Delete class="text-error h-6 w-6" />
                                        </button>
                                    </div>
                                </StatusItem>
                            {/snippet}
                        </DragDropList>
                    </div>
                </div>

                {#if editorOpen}
                    <div class="divider my-0"></div>
                    <form
                        onsubmit={saveNetwork}
                        novalidate
                        transition:slide|local={{ duration: 300, easing: cubicOut }}
                    >
                        <div
                            class="grid w-full grid-cols-1 content-center gap-x-4 px-4 sm:grid-cols-2"
                        >
                            <TextField
                                id="ssid"
                                label="SSID"
                                bind:value={draft.ssid}
                                error={editorErrors.ssid}
                            />
                            <TextField id="pwd" label="Password" error={editorErrors.password}>
                                {#snippet input()}
                                    <PasswordInput bind:value={draft.password} id="pwd" />
                                {/snippet}
                            </TextField>
                            <label
                                class="label inline-flex cursor-pointer content-end justify-start gap-4 mt-2 sm:mb-4"
                            >
                                <input
                                    id="staticIp"
                                    type="checkbox"
                                    bind:checked={draft.staticIp}
                                    class="checkbox checkbox-primary sm:-mb-5"
                                />
                                <span class="sm:-mb-5">Static IP Config?</span>
                            </label>
                        </div>
                        {#if draft.staticIp}
                            <div
                                class="grid w-full grid-cols-1 content-center gap-x-4 px-4 sm:grid-cols-2"
                                transition:slide|local={{ duration: 300, easing: cubicOut }}
                            >
                                <TextField
                                    id="localIP"
                                    label="Local IP"
                                    bind:value={draft.localIp}
                                    error={editorErrors.localIp}
                                />
                                <TextField
                                    id="gateway"
                                    label="Gateway IP"
                                    bind:value={draft.gatewayIp}
                                    error={editorErrors.gatewayIp}
                                />
                                <TextField
                                    id="subnet"
                                    label="Subnet Mask"
                                    bind:value={draft.subnetMask}
                                    error={editorErrors.subnetMask}
                                />
                                <TextField
                                    id="dns1"
                                    label="DNS 1"
                                    bind:value={draft.dnsIp1}
                                    error={editorErrors.dnsIp1}
                                />
                                <TextField
                                    id="dns2"
                                    label="DNS 2 (optional)"
                                    bind:value={draft.dnsIp2}
                                    error={editorErrors.dnsIp2}
                                />
                            </div>
                        {/if}
                        <div class="mx-4 mt-2 flex flex-wrap justify-end gap-2">
                            <button class="btn" type="button" onclick={() => (editorOpen = false)}>
                                Cancel
                            </button>
                            <button class="btn btn-primary" type="submit">Save network</button>
                        </div>
                    </form>
                {/if}

                <div class="divider mb-0"></div>
                <div
                    class="grid w-full grid-cols-1 content-center gap-x-4 px-4 sm:grid-cols-2"
                    transition:slide|local={{ duration: 300, easing: cubicOut }}
                >
                    <TextField
                        id="hostname"
                        label="Host Name"
                        bind:value={general.hostname}
                        error={hostnameProblem}
                    />
                    <label class="label inline-flex cursor-pointer content-end justify-start gap-4">
                        <input
                            type="checkbox"
                            bind:checked={general.priorityRssi}
                            class="checkbox checkbox-primary sm:-mb-5"
                        />
                        <span class="sm:-mb-5">Connect to strongest WiFi</span>
                    </label>
                </div>
                <div class="divider mb-2 mt-0"></div>
                <div class="mx-4 flex flex-wrap justify-end gap-2">
                    <button class="btn btn-primary" type="button" onclick={applyGeneral}>
                        Apply Settings
                    </button>
                </div>
            {/if}
        {:catch error}
            <LoadError {error} retry={() => (settingsLoad = getWifiSettings())} />
        {/await}
    </div>
</SettingsCard>
