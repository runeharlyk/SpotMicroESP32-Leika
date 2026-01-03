<script lang="ts">
    import { Cancel, Edit, EditOff, Power } from '$lib/components/icons'
    import { socket } from '$lib/stores'
    import { onMount } from 'svelte'
    import { modals } from 'svelte-modals'
    import ConfirmDialog from '$lib/components/ConfirmDialog.svelte'
    import {
        PeripheralSettingsData,
        PeripheralSettingsDataRequest
    } from '$lib/platform_shared/message'

    let settings: PeripheralSettingsData | null = $state(null)
    let isEditing = $state(false)

    onMount(() => {
        let unsub = socket.on(PeripheralSettingsData, handleSettings)
        socket.emit(PeripheralSettingsDataRequest, {})
        return unsub
    })

    const handleSettings = (data: PeripheralSettingsData) => {
        settings = data
    }

    const handleSave = () => {
        modals.open(ConfirmDialog, {
            title: 'Confirm configuration',
            message:
                'Are you sure you want to save this configuration? The operation cannot be undone. Please make sure you have the correct settings.',
            labels: {
                cancel: { label: 'Cancel', icon: Cancel },
                confirm: { label: 'Confirm', icon: Power }
            },
            onConfirm: () => {
                modals.close()
                socket.emit(PeripheralSettingsData, settings)
            }
        })
    }

    const Icon = $derived(isEditing ? EditOff : Edit)
</script>

{#if settings}
    <div class="collapse bg-base-100 border-base-300 border">
        <input type="checkbox" />
        <div class="collapse-title font-semibold">Configuration</div>
        <div class="collapse-content text-sm">
            <div class="flex flex-col gap-2">
                <label for="sda" class="input validator">
                    SDA

                    <input
                        id="sda"
                        type="number"
                        required
                        placeholder="Type a number between 1 to 48"
                        min="0"
                        max="48"
                        title="SDA pin number (0-48)"
                        disabled={!isEditing}
                        bind:value={settings.sda}
                    />
                </label>
                <label for="scl" class="input validator">
                    SCL

                    <input
                        id="scl"
                        type="number"
                        required
                        placeholder="Type a number between 1 to 48"
                        min="1"
                        max="48"
                        title="SCL pin number (0-48)"
                        disabled={!isEditing}
                        bind:value={settings.scl}
                    />
                </label>
                <label class="input validator" for="frequency">
                    Frequency
                    <input
                        id="frequency"
                        type="number"
                        required
                        placeholder="Type a number between 100000 to 430000"
                        min="100000"
                        max="430000"
                        title="I2C frequency in Hz"
                        disabled={!isEditing}
                        bind:value={settings.frequency}
                    />
                </label>
                <div>
                    <button
                        class="btn btn-outline btn-primary"
                        onclick={() => (isEditing = !isEditing)}
                    >
                        <Icon class="h-6 w-6" />
                    </button>
                    {#if isEditing}
                        <button class="btn btn-outline btn-primary" onclick={handleSave}
                            >Save</button
                        >
                    {/if}
                </div>
            </div>
        </div>
    </div>
{/if}
