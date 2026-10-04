<script lang="ts">
    import { onMount } from 'svelte'
    import { resolve } from '$app/paths'
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import SerialConsole from '$lib/components/SerialConsole.svelte'
    import VariantSelect from '$lib/components/VariantSelect.svelte'
    import { notifications } from '$lib/components/toasts/notifications'
    import { Chip, Usb, Wrench } from '$lib/components/icons'
    import {
        connectingSerial,
        connectionFeatures,
        socket,
        startSerialConnection
    } from '$lib/stores'
    import { renameConnectedRobot } from '$lib/services/robot-names'
    import { isSerialSupported, serialPortOpen } from '$lib/transport/serial-adapter'
    import Wifi from '../wifi/sta/Wifi.svelte'

    const transport = socket.transport
    const connected = $derived($socket && $transport === 'serial')

    // Ports this site was granted before, by this page or by the flasher, open without the chooser.
    let grantedPorts = $state<SerialPort[]>([])
    let nameDraft = $state('')
    let renaming = $state(false)

    onMount(() => {
        if (!isSerialSupported()) return
        const refresh = async () => (grantedPorts = await navigator.serial.getPorts())
        void refresh()
        navigator.serial.addEventListener('connect', refresh)
        navigator.serial.addEventListener('disconnect', refresh)
        return () => {
            navigator.serial.removeEventListener('connect', refresh)
            navigator.serial.removeEventListener('disconnect', refresh)
        }
    })

    const startRenaming = () => {
        nameDraft = $connectionFeatures?.robotName ?? ''
        renaming = true
    }

    const saveName = async () => {
        const error = await renameConnectedRobot(nameDraft)
        if (error) notifications.error(error, 4000)
        else renaming = false
    }
</script>

<div class="mx-0 my-1 flex flex-col space-y-4 sm:mx-8 sm:my-8">
    {#if !isSerialSupported()}
        <div role="alert" class="alert alert-warning alert-soft w-full max-w-2xl self-center">
            <span>
                Setup over USB needs Web Serial: Chrome or Edge on a desktop, with the app opened
                over https. The app the robot serves itself is plain http, so open the hosted app
                instead.
            </span>
        </div>
    {:else if connected}
        <SettingsCard collapsible={false}>
            {#snippet icon()}
                <Wrench class="mr-2 h-6 w-6 shrink-0 self-end" />
            {/snippet}
            {#snippet title()}
                <span>Robot</span>
            {/snippet}
            {#if renaming}
                <form
                    class="flex gap-2"
                    onsubmit={event => {
                        event.preventDefault()
                        saveName()
                    }}
                >
                    <input
                        class="input input-sm min-w-0 flex-1"
                        aria-label="Robot name"
                        maxlength="32"
                        bind:value={nameDraft}
                    />
                    <button class="btn btn-sm btn-primary" disabled={!nameDraft.trim()}>
                        Save
                    </button>
                    <button
                        class="btn btn-sm btn-ghost"
                        type="button"
                        onclick={() => (renaming = false)}
                    >
                        Cancel
                    </button>
                </form>
            {:else}
                <div class="flex items-center gap-2">
                    <span class="min-w-0 flex-1 truncate font-medium">
                        {$connectionFeatures?.robotName || 'Unnamed robot'}
                    </span>
                    <button
                        class="btn btn-ghost btn-sm"
                        onclick={startRenaming}
                        disabled={!$connectionFeatures}
                    >
                        Rename
                    </button>
                </div>
            {/if}
            {#if $connectionFeatures}
                <VariantSelect features={$connectionFeatures} />
            {/if}
            <a class="btn btn-ghost btn-sm justify-start" href={resolve('/peripherals/sensors')}>
                <Chip class="h-4 w-4" />
                Sensors and pins
            </a>
        </SettingsCard>

        <Wifi />
    {:else if $serialPortOpen}
        <div
            class="bg-base-200 rounded-box flex w-full max-w-2xl items-center gap-3 self-center p-4"
        >
            <span class="loading loading-spinner loading-sm"></span>
            <span>
                The port is open; waiting for the robot to answer. A board resets when its port
                opens, so this takes a few seconds. If it never answers, its log below shows why.
            </span>
        </div>
    {:else}
        <div class="bg-base-200 rounded-box flex w-full max-w-2xl flex-col gap-3 self-center p-4">
            <p>
                Connect the robot to this computer with a USB data cable to set up its wifi, variant
                and name without joining its access point.
            </p>
            {#if grantedPorts.length}
                <button
                    class="btn btn-primary"
                    onclick={() => startSerialConnection(grantedPorts[0])}
                    disabled={$connectingSerial}
                >
                    <Usb class="h-5 w-5" />
                    Connect to the board used before
                </button>
            {/if}
            <button
                class="btn {grantedPorts.length ? 'btn-ghost' : 'btn-primary'}"
                onclick={() => startSerialConnection()}
                disabled={$connectingSerial}
            >
                {#if $connectingSerial}
                    <span class="loading loading-spinner loading-xs"></span>
                {:else}
                    <Usb class="h-5 w-5" />
                {/if}
                {grantedPorts.length ? 'Choose another port' : 'Connect over USB'}
            </button>
            <p class="text-xs opacity-60">
                Only one program can hold the port: close the flasher's dialog and any serial
                monitor first.
            </p>
        </div>
    {/if}

    {#if isSerialSupported()}
        <div class="w-full max-w-2xl self-center">
            <SerialConsole />
        </div>
    {/if}
</div>
