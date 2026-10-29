<script lang="ts">
    import { onDestroy } from 'svelte'
    import { slide } from 'svelte/transition'
    import { cubicOut } from 'svelte/easing'
    import { PasswordInput, TextField } from '$lib/components/input'
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { notifications } from '$lib/components/toasts/notifications'
    import Spinner from '$lib/components/Spinner.svelte'
    import LoadError from '$lib/components/LoadError.svelte'
    import { robotRequest } from '$lib/robot-request'
    import { ipToUint32, uint32ToIp } from '$lib/utilities'
    import { accessPointErrors, type AccessPointDraft } from '$lib/network-settings'
    import { AP, Devices, Home, MAC, Warning } from '$lib/components/icons'
    import StatusItem from '$lib/components/StatusItem.svelte'
    import { APSettings, APStatus } from '$lib/platform_shared/api'
    import { SettingsForm } from '$lib/robot/settings.svelte'

    let apStatus: APStatus | null = $state(null)

    // The settings as the robot stores them, as it pushes them, and the form's copy, with addresses as text; the
    // robot's settings change only through a save.
    const form = new SettingsForm(
        APSettings,
        (stored): AccessPointDraft & Pick<APSettings, 'provisionMode' | 'ssidHidden'> => ({
            ssid: stored.ssid,
            password: stored.password,
            channel: stored.channel,
            maxClients: stored.maxClients,
            localIp: uint32ToIp(stored.localIp),
            gatewayIp: uint32ToIp(stored.gatewayIp),
            subnetMask: uint32ToIp(stored.subnetMask),
            provisionMode: stored.provisionMode,
            ssidHidden: stored.ssidHidden
        })
    )
    $effect(() => form.follow())
    const apSettings = $derived(form.stored)
    const draft = $derived(form.draft)
    let errors = $state<ReturnType<typeof accessPointErrors>>({})

    async function getAPStatus() {
        const reply = await robotRequest({ apStatusRequest: {} })
        if (!reply.apStatus) throw new Error('The robot sent no access point status')
        apStatus = reply.apStatus
    }

    let statusLoad = $state(getAPStatus())

    let pollError = $state<unknown>()

    const pollStatus = () =>
        getAPStatus().then(
            () => (pollError = undefined),
            error => (pollError = error)
        )

    const interval = setInterval(pollStatus, 5000)

    onDestroy(() => clearInterval(interval))

    let provisionMode = [
        {
            id: 0,
            text: `Always`
        },
        {
            id: 1,
            text: `When WiFi Disconnected`
        },
        {
            id: 2,
            text: `Never`
        }
    ]

    type Variant = 'success' | 'error' | 'primary' | 'info' | 'warning'

    let apStatusVariant: Variant[] = ['success', 'error', 'warning']

    let apStatusDescription = ['Active', 'Inactive', 'Lingering']

    async function postAPSettings(data: APSettings) {
        try {
            await form.save(() => robotRequest({ apSettings: data }))
            notifications.success('Access Point settings updated.', 3000)
        } catch (error) {
            notifications.error(
                `Saving access point settings failed: ${(error as Error).message}`,
                5000
            )
        }
    }

    function handleSubmitAP(event: Event) {
        event.preventDefault()
        if (!apSettings || !draft) return
        errors = accessPointErrors(draft)
        if (Object.keys(errors).length) return
        postAPSettings({
            ...$state.snapshot(apSettings),
            ssid: draft.ssid,
            password: draft.password,
            channel: draft.channel,
            maxClients: draft.maxClients,
            localIp: ipToUint32(draft.localIp),
            gatewayIp: ipToUint32(draft.gatewayIp),
            subnetMask: ipToUint32(draft.subnetMask),
            provisionMode: draft.provisionMode,
            ssidHidden: draft.ssidHidden
        })
    }
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <AP class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    {/snippet}
    {#snippet title()}
        <span>Access Point</span>
    {/snippet}
    <div class="w-full overflow-x-auto">
        {#await statusLoad}
            <Spinner />
        {:then}
            {#if pollError}
                <LoadError error={pollError} retry={pollStatus} />
            {:else if apStatus}
                <div
                    class="flex w-full flex-col space-y-1"
                    transition:slide|local={{ duration: 300, easing: cubicOut }}
                >
                    <StatusItem
                        icon={AP}
                        title="Status"
                        variant={apStatusVariant[apStatus.status]}
                        description={apStatusDescription[apStatus.status]}
                    />

                    <StatusItem
                        icon={Home}
                        title="IP Address"
                        description={uint32ToIp(apStatus.ipAddress)}
                    />

                    <StatusItem icon={MAC} title="MAC Address" description={apStatus.macAddress} />

                    <StatusItem
                        icon={Devices}
                        title="AP Clients"
                        description={apStatus.stationNum}
                    />
                </div>
            {/if}
        {:catch error}
            <LoadError {error} retry={() => (statusLoad = getAPStatus())} />
        {/await}
    </div>

    <div class="bg-base-200 relative grid w-full max-w-2xl self-center overflow-hidden">
        <div
            class="min-h-16 flex w-full items-center justify-between space-x-3 p-0 text-xl font-medium"
        >
            Change AP Settings
        </div>
        {#if apSettings && draft}
            <div
                class="flex flex-col gap-2 p-0"
                transition:slide|local={{ duration: 300, easing: cubicOut }}
            >
                <form
                    class="grid w-full grid-cols-1 content-center gap-x-4 p-0s sm:grid-cols-2"
                    onsubmit={handleSubmitAP}
                    novalidate
                >
                    <div>
                        <label class="label" for="apmode">
                            <span class="label-text">Provide Access Point ...</span>
                        </label>
                        <select
                            class="select select-bordered w-full"
                            id="apmode"
                            bind:value={draft.provisionMode}
                        >
                            {#each provisionMode as mode (mode.id)}
                                <option value={mode.id}>
                                    {mode.text}
                                </option>
                            {/each}
                        </select>
                    </div>
                    <TextField
                        id="ssid"
                        label="SSID"
                        bind:value={draft!.ssid}
                        error={errors.ssid}
                    />
                    <TextField id="pwd" label="Password" error={errors.password}>
                        {#snippet input()}
                            <PasswordInput bind:value={draft!.password} id="pwd" />
                        {/snippet}
                    </TextField>
                    <TextField
                        id="channel"
                        label="Preferred Channel"
                        numeric
                        bind:value={draft!.channel}
                        error={errors.channel}
                    />
                    <TextField
                        id="clients"
                        label="Max Clients"
                        numeric
                        bind:value={draft!.maxClients}
                        error={errors.maxClients}
                    />
                    <TextField
                        id="localIP"
                        label="Local IP"
                        bind:value={draft!.localIp}
                        error={errors.localIp}
                    />
                    <TextField
                        id="gateway"
                        label="Gateway IP"
                        bind:value={draft!.gatewayIp}
                        error={errors.gatewayIp}
                    />
                    <TextField
                        id="subnet"
                        label="Subnet Mask"
                        bind:value={draft!.subnetMask}
                        error={errors.subnetMask}
                    />

                    <label class="label my-auto cursor-pointer justify-start gap-4">
                        <input
                            type="checkbox"
                            bind:checked={draft.ssidHidden}
                            class="checkbox checkbox-primary"
                        />
                        <span class="">Hide SSID</span>
                    </label>

                    {#if form.changedOnRobot}
                        <p class="text-warning text-sm" data-testid="changed-on-robot">
                            Changed on the robot meanwhile.
                            <button class="link" type="button" onclick={() => form.reset()}>
                                Show the robot's
                            </button>
                        </p>
                    {/if}
                    <div class="place-self-end">
                        <button class="btn btn-primary" type="submit" disabled={form.saving}>
                            Apply Settings
                        </button>
                    </div>
                </form>
            </div>
        {:else if form.problem}
            <div role="alert" class="alert alert-error alert-soft">
                <Warning class="h-6 w-6 shrink-0" />
                <span>{form.problem}</span>
            </div>
        {:else}
            <Spinner />
        {/if}
    </div>
</SettingsCard>
