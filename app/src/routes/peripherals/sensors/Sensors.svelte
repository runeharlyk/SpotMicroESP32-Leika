<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import Spinner from '$lib/components/Spinner.svelte'
    import LoadError from '$lib/components/LoadError.svelte'
    import { Chip } from '$lib/components/icons'
    import { notifications } from '$lib/components/toasts/notifications'
    import { robotRequest } from '$lib/robot-request'
    import { applyFeatures, connectionFeatures } from '$lib/stores/featureFlags'
    import {
        SENSOR_STATUS_LABELS,
        formFromSettings,
        formProblem,
        sensorRows,
        settingsFromForm,
        type PeripheralsForm,
        type SensorStatus
    } from '$lib/peripherals'
    import type { PeripheralSettings } from '$lib/platform_shared/api'

    const STATUS_BADGES: Record<SensorStatus, string> = {
        active: 'badge-success',
        'detected-but-disabled': 'badge-warning',
        disabled: 'badge-neutral',
        absent: 'badge-ghost'
    }

    let settings = $state<PeripheralSettings | null>(null)
    let form = $state<PeripheralsForm | null>(null)
    let saving = $state(false)

    const applySettings = (received: PeripheralSettings) => {
        settings = received
        form = formFromSettings(received)
    }

    // The robot re-probes its sensors when the settings change, so what it detected is asked anew.
    const refreshFeatures = async () => {
        const reply = await robotRequest({ featuresDataRequest: {} })
        if (!reply.featuresDataResponse) throw new Error('The robot sent no feature flags')
        applyFeatures(reply.featuresDataResponse)
    }

    const load = async () => {
        const [reply] = await Promise.all([
            robotRequest({ peripheralSettingsRequest: {} }),
            refreshFeatures()
        ])
        if (!reply.peripheralSettings) throw new Error('The robot sent no peripheral settings')
        applySettings(reply.peripheralSettings)
    }

    let loading = $state(load())

    const rows = $derived(
        $connectionFeatures && settings ? sensorRows($connectionFeatures, settings) : []
    )
    const problem = $derived(settings && form ? formProblem(settings, form) : null)

    const save = async () => {
        if (!settings || !form) return
        saving = true
        try {
            const reply = await robotRequest({
                peripheralSettings: settingsFromForm(settings, form)
            })
            if (reply.peripheralSettings) applySettings(reply.peripheralSettings)
            notifications.success('Sensor settings saved', 3000)
        } catch (error) {
            notifications.error(`Saving sensor settings failed: ${(error as Error).message}`, 5000)
            return
        } finally {
            saving = false
        }
        await refreshFeatures().catch(() =>
            notifications.error('What the robot detected could not be fetched again', 5000)
        )
    }
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Chip class="mr-2 h-6 w-6 flex-shrink-0 self-end" />
    {/snippet}
    {#snippet title()}
        <span>Sensors</span>
    {/snippet}

    {#await loading}
        <Spinner />
    {:catch error}
        <LoadError {error} retry={() => (loading = load())} />
    {/await}

    {#if settings && form}
        <div class="overflow-x-auto">
            <table class="table table-sm">
                <thead>
                    <tr>
                        <th>Sensor</th>
                        <th>Status</th>
                        <th>Use</th>
                    </tr>
                </thead>
                <tbody>
                    {#each rows as row (row.sensor)}
                        <tr>
                            <td class="font-medium">{row.label}</td>
                            <td>
                                <span class="badge badge-sm {STATUS_BADGES[row.status]}">
                                    {SENSOR_STATUS_LABELS[row.status]}
                                </span>
                            </td>
                            <td>
                                {#if row.toggle}
                                    <label class="flex items-center gap-2">
                                        <input
                                            type="checkbox"
                                            class="toggle toggle-sm toggle-primary"
                                            aria-label="Use the {row.label.toLowerCase()}"
                                            bind:checked={form.use[row.toggle]}
                                        />
                                        {#if row.appliesOnRestart}
                                            <span class="text-xs opacity-70">
                                                Applies after a restart
                                            </span>
                                        {/if}
                                    </label>
                                {/if}
                            </td>
                        </tr>
                    {/each}
                </tbody>
            </table>
        </div>
        <p class="text-xs opacity-70">
            A disabled sensor is not probed, so the robot may not know whether it is fitted.
        </p>

        {#if form.ws2812}
            <div class="divider my-1">WS2812 LEDs</div>
            <div class="flex flex-wrap items-center gap-4">
                <label class="label gap-2">
                    <input
                        type="checkbox"
                        class="toggle toggle-sm toggle-primary"
                        bind:checked={form.ws2812.enabled}
                    />
                    Enabled
                </label>
                <label class="input input-sm w-32">
                    Pin
                    <input
                        type="number"
                        min="0"
                        disabled={!form.ws2812.enabled}
                        bind:value={form.ws2812.pin}
                    />
                </label>
            </div>
        {/if}

        {#if problem}
            <div role="alert" class="alert alert-error alert-soft">{problem}; not saved.</div>
        {/if}
        <div>
            <button class="btn btn-primary btn-sm" onclick={save} disabled={saving || !!problem}>
                {#if saving}
                    <span class="loading loading-spinner loading-xs"></span>
                {/if}
                Save
            </button>
        </div>
    {/if}
</SettingsCard>
