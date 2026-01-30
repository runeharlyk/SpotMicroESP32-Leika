<script lang="ts">
    import { modals } from 'svelte-modals'
    import { slide } from 'svelte/transition'
    import { cubicOut } from 'svelte/easing'
    import { notifications } from '$lib/components/toasts/notifications'
    import DragDropList, { VerticalDropZone, reorder, type DropEvent } from 'svelte-dnd-list'
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { PasswordInput } from '$lib/components/input'
    import ConfirmDialog from '$lib/components/ConfirmDialog.svelte'
    import ScanNetworks from './Scan.svelte'
    import Spinner from '$lib/components/Spinner.svelte'
    import InfoDialog from '$lib/components/InfoDialog.svelte'
    import {
        type WifiStatus,
        type WifiSettings,
        type WifiNetwork,
        type Response as ProtoResponse,
        Request
    } from '$lib/platform_shared/api'
    import { api } from '$lib/api'
    import { ipToUint32, uint32ToIp, isValidIpString } from '$lib/utilities'
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

    let networkEditable: WifiNetwork = $state({
        ssid: '',
        password: '',
        staticIpConfig: false,
        localIp: 0,
        subnetMask: 0,
        gatewayIp: 0,
        dnsIp1: 0,
        dnsIp2: 0
    })

    let ipDisplay = $state({
        localIp: '',
        subnetMask: '',
        gatewayIp: '',
        dnsIp1: '',
        dnsIp2: ''
    })

    let staticIpConfig = $state(false)

    let newNetwork: boolean = $state(true)
    let showNetworkEditor: boolean = $state(false)

    let wifiStatus: WifiStatus | null = $state(null)
    let wifiSettings: WifiSettings | null = $state(null)

    let dndNetworkList: WifiNetwork[] = $state([])

    let showWifiDetails = $state(false)

    let formField: Record<string, unknown> = $state({})

    let formErrors = $state({
        ssid: false,
        localIp: false,
        gatewayIp: false,
        subnetMask: false,
        dnsIp1: false,
        dnsIp2: false
    })

    let formErrorhostname = $state(false)

    async function getWifiStatus() {
        const result = await api.get<ProtoResponse>('/api/wifi/sta/status')
        if (result.isErr()) {
            console.error(`Error occurred while fetching: `, result.inner)
            return
        }
        if (result.inner.wifiStatus) {
            wifiStatus = result.inner.wifiStatus
        }
        return wifiStatus
    }

    async function getWifiSettings() {
        const result = await api.get<ProtoResponse>('/api/wifi/sta/settings')
        if (result.isErr()) {
            console.error(`Error occurred while fetching: `, result.inner)
            return
        }
        wifiSettings = result.inner.wifiSettings!
        dndNetworkList = wifiSettings.wifiNetworks
        return wifiSettings
    }

    async function postWiFiSettings(data: WifiSettings) {
        const result = await api.post_proto<ProtoResponse>('/api/wifi/sta/settings', Request.create({ wifiSettings: data }))
        if (result.isErr()) {
            console.error(`Error occurred while fetching: `, result.inner)
            notifications.error('User not authorized.', 3000)
            return
        }
        if (result.inner.statusCode !== 200) {
            notifications.error(result.inner.errorMessage || 'Failed to update settings', 3000)
            return
        }
        if (result.inner.wifiSettings) {
            wifiSettings = result.inner.wifiSettings
        }
        notifications.success('Wi-Fi settings updated.', 3000)
    }

    function validateHostName() {
        if (!wifiSettings) return false
        if (wifiSettings.hostname.length < 3 || wifiSettings.hostname.length > 32) {
            formErrorhostname = true
        } else {
            formErrorhostname = false
            // Update global wifiSettings object
            wifiSettings.wifiNetworks = dndNetworkList
            // Post to REST API
            postWiFiSettings(wifiSettings)
            console.log(wifiSettings)
        }
    }

    function validateWiFiForm(event: SubmitEvent) {
        event.preventDefault()
        let valid = true

        if (networkEditable.ssid.length < 3 || networkEditable.ssid.length > 32) {
            valid = false
            formErrors.ssid = true
        } else {
            formErrors.ssid = false
        }

        networkEditable.staticIpConfig = staticIpConfig

        if (networkEditable.staticIpConfig) {
            if (!isValidIpString(ipDisplay.gatewayIp)) {
                valid = false
                formErrors.gatewayIp = true
            } else {
                formErrors.gatewayIp = false
            }

            if (!isValidIpString(ipDisplay.subnetMask)) {
                valid = false
                formErrors.subnetMask = true
            } else {
                formErrors.subnetMask = false
            }

            if (!isValidIpString(ipDisplay.localIp)) {
                valid = false
                formErrors.localIp = true
            } else {
                formErrors.localIp = false
            }

            if (!isValidIpString(ipDisplay.dnsIp1)) {
                valid = false
                formErrors.dnsIp1 = true
            } else {
                formErrors.dnsIp1 = false
            }

            if (!isValidIpString(ipDisplay.dnsIp2)) {
                valid = false
                formErrors.dnsIp2 = true
            } else {
                formErrors.dnsIp2 = false
            }

            networkEditable.localIp = ipToUint32(ipDisplay.localIp)
            networkEditable.subnetMask = ipToUint32(ipDisplay.subnetMask)
            networkEditable.gatewayIp = ipToUint32(ipDisplay.gatewayIp)
            networkEditable.dnsIp1 = ipToUint32(ipDisplay.dnsIp1)
            networkEditable.dnsIp2 = ipToUint32(ipDisplay.dnsIp2)
        } else {
            formErrors.localIp = false
            formErrors.subnetMask = false
            formErrors.gatewayIp = false
            formErrors.dnsIp1 = false
            formErrors.dnsIp2 = false
        }

        if (valid) {
            if (newNetwork) {
                dndNetworkList.push(networkEditable)
            } else {
                dndNetworkList.splice(dndNetworkList.indexOf(networkEditable), 1, networkEditable)
            }
            addNetwork()
            dndNetworkList = [...dndNetworkList]
            showNetworkEditor = false
            if (wifiSettings) {
                wifiSettings.wifiNetworks = dndNetworkList
                postWiFiSettings(wifiSettings)
            }
        }
    }

    function scanForNetworks() {
        modals.open(ScanNetworks, {
            storeNetwork: (network: string) => {
                addNetwork()
                networkEditable.ssid = network
                showNetworkEditor = true
                modals.close()
            }
        })
    }

    function addNetwork() {
        newNetwork = true
        networkEditable = {
            ssid: '',
            password: '',
            staticIpConfig: false,
            localIp: 0,
            subnetMask: 0,
            gatewayIp: 0,
            dnsIp1: 0,
            dnsIp2: 0
        }
        ipDisplay = {
            localIp: '',
            subnetMask: '',
            gatewayIp: '',
            dnsIp1: '',
            dnsIp2: ''
        }
    }

    function handleEdit(index: number) {
        newNetwork = false
        showNetworkEditor = true
        networkEditable = dndNetworkList[index]
        ipDisplay = {
            localIp: networkEditable.localIp ? uint32ToIp(networkEditable.localIp) : '',
            subnetMask: networkEditable.subnetMask ? uint32ToIp(networkEditable.subnetMask) : '',
            gatewayIp: networkEditable.gatewayIp ? uint32ToIp(networkEditable.gatewayIp) : '',
            dnsIp1: networkEditable.dnsIp1 ? uint32ToIp(networkEditable.dnsIp1) : '',
            dnsIp2: networkEditable.dnsIp2 ? uint32ToIp(networkEditable.dnsIp2) : ''
        }
    }

    function confirmDelete(index: number) {
        modals.open(ConfirmDialog, {
            title: 'Delete Network',
            message: 'Are you sure you want to delete this network?',
            labels: {
                cancel: { label: 'Cancel', icon: Cancel },
                confirm: { label: 'Delete', icon: Delete }
            },
            onConfirm: () => {
                // Check if network is currently been edited and delete as well
                if (dndNetworkList[index].ssid === networkEditable.ssid) {
                    addNetwork()
                }
                // Remove network from array
                dndNetworkList.splice(index, 1)
                dndNetworkList = [...dndNetworkList] //Trigger reactivity
                showNetworkEditor = false
                modals.close()
            }
        })
    }

    function checkNetworkList() {
        if (dndNetworkList.length >= 5) {
            modals.open(InfoDialog, {
                title: 'Reached Maximum Networks',
                message:
                    'You have reached the maximum number of networks. Please delete one to add another.',
                dismiss: { label: 'OK', icon: Check },
                onDismiss: () => modals.close()
            })
            return false
        } else {
            return true
        }
    }

    function onDrop({ detail: { from, to } }: CustomEvent<DropEvent>) {
        if (!to || from === to) {
            return
        }

        dndNetworkList = reorder(dndNetworkList, from.index, to.index)
        console.log(dndNetworkList)
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
        {#await getWifiStatus()}
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
        {/await}
    </div>

    <div class="bg-base-200 relative grid w-full max-w-2xl self-center overflow-hidden">
        <div
            class="min-h-16 flex w-full items-center justify-between space-x-3 p-0 text-xl font-medium"
        >
            Saved Networks
        </div>
        {#await getWifiSettings()}
            <Spinner />
        {:then}
            {#if wifiSettings}
                <div class="relative w-full overflow-visible">
                    <button
                        class="btn btn-primary text-primary-content btn-md absolute -top-14 right-16"
                        onclick={() => {
                            if (checkNetworkList()) {
                                addNetwork()
                                showNetworkEditor = true
                            }
                        }}
                    >
                        <Add class="h-6 w-6" /></button
                    >
                    <button
                        class="btn btn-primary text-primary-content btn-md absolute -top-14 right-0"
                        onclick={() => {
                            if (checkNetworkList()) {
                                scanForNetworks()
                                showNetworkEditor = true
                            }
                        }}
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
                            itemCount={dndNetworkList.length}
                            on:drop={onDrop}
                        >
                            {#snippet children({ index }: { index: number })}
                                <StatusItem icon={Router} title={dndNetworkList[index].ssid}>
                                    <div class="space-x-0 px-0 mx-0">
                                        <button
                                            class="btn btn-ghost btn-sm"
                                            onclick={() => {
                                                handleEdit(index)
                                            }}
                                        >
                                            <Edit class="h-6 w-6" /></button
                                        >
                                        <button
                                            class="btn btn-ghost btn-sm"
                                            onclick={() => {
                                                confirmDelete(index)
                                            }}
                                        >
                                            <Delete class="text-error h-6 w-6" />
                                        </button>
                                    </div>
                                </StatusItem>
                            {/snippet}
                        </DragDropList>
                    </div>
                </div>

                <div class="divider mb-0"></div>
                <div
                    class="flex flex-col gap-2 p-0"
                    transition:slide|local={{ duration: 300, easing: cubicOut }}
                >
                    <form class="" onsubmit={validateWiFiForm} novalidate bind:this={formField}>
                        <div
                            class="grid w-full grid-cols-1 content-center gap-x-4 px-4 sm:grid-cols-2"
                        >
                            <div>
                                <label class="label" for="channel">
                                    <span class="label-text text-md">Host Name</span>
                                </label>
                                <input
                                    type="text"
                                    min="1"
                                    max="32"
                                    class="input input-bordered invalid:border-error w-full invalid:border-2 {(
                                        formErrorhostname
                                    ) ?
                                        'border-error border-2'
                                    :   ''}"
                                    bind:value={wifiSettings.hostname}
                                    id="channel"
                                    required
                                />
                                <label class="label" for="channel">
                                    <span
                                        class="label-text-alt text-error {formErrorhostname ? '' : (
                                            'hidden'
                                        )}">Host name must be between 2 and 32 characters long</span
                                    >
                                </label>
                            </div>
                            <label
                                class="label inline-flex cursor-pointer content-end justify-start gap-4"
                            >
                                <input
                                    type="checkbox"
                                    bind:checked={wifiSettings.priorityRssi}
                                    class="checkbox checkbox-primary sm:-mb-5"
                                />
                                <span class="sm:-mb-5">Connect to strongest WiFi</span>
                            </label>
                        </div>

                        {#if showNetworkEditor}
                            <div class="divider my-0"></div>
                            <div
                                class="grid w-full grid-cols-1 content-center gap-x-4 px-4 sm:grid-cols-2"
                                transition:slide|local={{ duration: 300, easing: cubicOut }}
                            >
                                <div>
                                    <label class="label" for="ssid">
                                        <span class="label-text text-md">SSID</span>
                                    </label>
                                    <input
                                        type="text"
                                        class="input input-bordered invalid:border-error w-full invalid:border-2 {(
                                            formErrors.ssid
                                        ) ?
                                            'border-error border-2'
                                        :   ''}"
                                        bind:value={networkEditable.ssid}
                                        id="ssid"
                                        min="2"
                                        max="32"
                                        required
                                    />
                                    <label class="label" for="ssid">
                                        <span
                                            class="label-text-alt text-error {formErrors.ssid ? ''
                                            :   'hidden'}"
                                            >SSID must be between 3 and 32 characters long</span
                                        >
                                    </label>
                                </div>
                                <div>
                                    <label class="label" for="pwd">
                                        <span class="label-text text-md">Password</span>
                                    </label>
                                    <PasswordInput bind:value={networkEditable.password} id="pwd" />
                                </div>
                                <label
                                    class="label inline-flex cursor-pointer content-end justify-start gap-4 mt-2 sm:mb-4"
                                >
                                    <input
                                        type="checkbox"
                                        bind:checked={staticIpConfig}
                                        class="checkbox checkbox-primary sm:-mb-5"
                                    />
                                    <span class="sm:-mb-5">Static IP Config?</span>
                                </label>
                            </div>
                            {#if staticIpConfig}
                                <div
                                    class="grid w-full grid-cols-1 content-center gap-x-4 px-4 sm:grid-cols-2"
                                    transition:slide|local={{ duration: 300, easing: cubicOut }}
                                >
                                    <div>
                                        <label class="label" for="localIP">
                                            <span class="label-text text-md">Local IP</span>
                                        </label>
                                        <input
                                            type="text"
                                            class="input input-bordered w-full {(
                                                formErrors.localIp
                                            ) ?
                                                'border-error border-2'
                                            :   ''}"
                                            minlength="7"
                                            maxlength="15"
                                            size="15"
                                            bind:value={ipDisplay.localIp}
                                            id="localIP"
                                            required
                                        />
                                        <label class="label" for="localIP">
                                            <span
                                                class="label-text-alt text-error {(
                                                    formErrors.localIp
                                                ) ?
                                                    ''
                                                :   'hidden'}">Must be a valid IPv4 address</span
                                            >
                                        </label>
                                    </div>

                                    <div>
                                        <label class="label" for="gateway">
                                            <span class="label-text text-md">Gateway IP</span>
                                        </label>
                                        <input
                                            type="text"
                                            class="input input-bordered w-full {(
                                                formErrors.gatewayIp
                                            ) ?
                                                'border-error border-2'
                                            :   ''}"
                                            minlength="7"
                                            maxlength="15"
                                            size="15"
                                            bind:value={ipDisplay.gatewayIp}
                                            required
                                        />
                                        <label class="label" for="gateway">
                                            <span
                                                class="label-text-alt text-error {(
                                                    formErrors.gatewayIp
                                                ) ?
                                                    ''
                                                :   'hidden'}">Must be a valid IPv4 address</span
                                            >
                                        </label>
                                    </div>
                                    <div>
                                        <label class="label" for="subnet">
                                            <span class="label-text text-md">Subnet Mask</span>
                                        </label>
                                        <input
                                            type="text"
                                            class="input input-bordered w-full {(
                                                formErrors.subnetMask
                                            ) ?
                                                'border-error border-2'
                                            :   ''}"
                                            minlength="7"
                                            maxlength="15"
                                            size="15"
                                            bind:value={ipDisplay.subnetMask}
                                            required
                                        />
                                        <label class="label" for="subnet">
                                            <span
                                                class="label-text-alt text-error {(
                                                    formErrors.subnetMask
                                                ) ?
                                                    ''
                                                :   'hidden'}"
                                            >
                                                Must be a valid IPv4 address
                                            </span>
                                        </label>
                                    </div>
                                    <div>
                                        <label class="label" for="gateway">
                                            <span class="label-text text-md">DNS 1</span>
                                        </label>
                                        <input
                                            type="text"
                                            class="input input-bordered w-full {formErrors.dnsIp1 ?
                                                'border-error border-2'
                                            :   ''}"
                                            minlength="7"
                                            maxlength="15"
                                            size="15"
                                            bind:value={ipDisplay.dnsIp1}
                                            required
                                        />
                                        <label class="label" for="gateway">
                                            <span
                                                class="label-text-alt text-error {formErrors.dnsIp1 ?
                                                    ''
                                                :   'hidden'}"
                                            >
                                                Must be a valid IPv4 address
                                            </span>
                                        </label>
                                    </div>
                                    <div>
                                        <label class="label" for="subnet">
                                            <span class="label-text text-md">DNS 2</span>
                                        </label>
                                        <input
                                            type="text"
                                            class="input input-bordered w-full {formErrors.dnsIp2 ?
                                                'border-error border-2'
                                            :   ''}"
                                            minlength="7"
                                            maxlength="15"
                                            size="15"
                                            bind:value={ipDisplay.dnsIp2}
                                            required
                                        />
                                        <label class="label" for="subnet">
                                            <span
                                                class="label-text-alt text-error {formErrors.dnsIp2 ?
                                                    ''
                                                :   'hidden'}"
                                            >
                                                Must be a valid IPv4 address
                                            </span>
                                        </label>
                                    </div>
                                </div>
                            {/if}
                        {/if}

                        <div class="divider mb-2 mt-0"></div>
                        <div class="mx-4 flex flex-wrap justify-end gap-2">
                            <button
                                class="btn btn-primary"
                                type="submit"
                                disabled={!showNetworkEditor}
                            >
                                {newNetwork ? 'Add Network' : 'Update Network'}
                            </button>
                            <button
                                class="btn btn-primary"
                                type="button"
                                onclick={validateHostName}
                            >
                                Apply Settings
                            </button>
                        </div>
                    </form>
                </div>
            {/if}
        {/await}
    </div>
</SettingsCard>
