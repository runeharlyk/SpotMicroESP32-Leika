<script lang="ts">
    import { Cancel, Edit, EditOff, Power, Warning } from '$lib/components/icons'
    import { robotRequest } from '$lib/robot-request'
    import { modals } from 'svelte-modals'
    import ConfirmDialog from '$lib/components/ConfirmDialog.svelte'
    import Spinner from '$lib/components/Spinner.svelte'
    import { notifications } from '$lib/components/toasts/notifications'
    import { PeripheralSettings } from '$lib/platform_shared/api'
    import { SettingsForm } from '$lib/robot/settings.svelte'

    // The stored settings as the robot pushes them; the form edits a copy.
    const form = new SettingsForm(PeripheralSettings, stored => stored)
    $effect(() => form.follow())
    const settings = $derived(form.draft)
    let isEditing = $state(false)

    const handleSave = () => {
        modals.open(ConfirmDialog, {
            title: 'Confirm configuration',
            message:
                'Are you sure you want to save this configuration? The operation cannot be undone. Please make sure you have the correct settings.',
            labels: {
                cancel: { label: 'Cancel', icon: Cancel },
                confirm: { label: 'Confirm', icon: Power }
            },
            onConfirm: async () => {
                modals.close()
                if (!settings) return
                const saved = $state.snapshot(settings)
                try {
                    await form.save(() => robotRequest({ peripheralSettings: saved }))
                    isEditing = false
                } catch (error) {
                    notifications.error(
                        `Saving I2C settings failed: ${(error as Error).message}`,
                        5000
                    )
                }
            }
        })
    }

    const Icon = $derived(isEditing ? EditOff : Edit)
</script>

{#if !settings && form.problem}
    <div role="alert" class="alert alert-error alert-soft">
        <Warning class="h-6 w-6 shrink-0" />
        <span>{form.problem}</span>
    </div>
{:else if !settings}
    <Spinner />
{/if}

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
                        aria-label={isEditing ? 'Stop editing' : 'Edit'}
                    >
                        <Icon class="h-6 w-6" />
                    </button>
                    {#if isEditing}
                        <button
                            class="btn btn-outline btn-primary"
                            onclick={handleSave}
                            disabled={form.saving}>Save</button
                        >
                    {/if}
                    {#if form.changedOnRobot}
                        <p class="text-warning text-sm" data-testid="changed-on-robot">
                            Changed on the robot meanwhile.
                            <button class="link" type="button" onclick={() => form.reset()}>
                                Show the robot's
                            </button>
                        </p>
                    {/if}
                </div>
            </div>
        </div>
    </div>
{/if}
