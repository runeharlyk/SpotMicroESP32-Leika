<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import Spinner from '$lib/components/Spinner.svelte'
    import LoadError from '$lib/components/LoadError.svelte'
    import { Chip } from '$lib/components/icons'
    import { notifications } from '$lib/components/toasts/notifications'
    import { robotRequest } from '$lib/robot-request'
    import { applyFeatures, connectionFeatures } from '$lib/stores/featureFlags'
    import {
        POWER_ADDRESSES,
        SENSOR_STATUS_LABELS,
        formFromSettings,
        formProblem,
        sensorRows,
        settingsFromForm,
        type PeripheralsForm,
        type SensorStatus
    } from '$lib/peripherals'
    import { PeripheralSettings } from '$lib/platform_shared/api'
    import { SettingsForm } from '$lib/robot/settings.svelte'

    const STATUS_BADGES: Record<SensorStatus, string> = {
        active: 'badge-success',
        'detected-but-disabled': 'badge-warning',
        disabled: 'badge-neutral',
        absent: 'badge-ghost'
    }

    // The stored settings as the robot pushes them, and the form's copy of them.
    const peripherals = new SettingsForm<typeof PeripheralSettings, PeripheralsForm>(
        PeripheralSettings,
        formFromSettings
    )
    $effect(() => peripherals.follow())
    const settings = $derived(peripherals.stored)
    const form = $derived(peripherals.draft)

    // The robot re-probes its sensors when the settings change, so what it detected is asked anew.
    const refreshFeatures = async () => {
        const reply = await robotRequest({ featuresDataRequest: {} })
        if (!reply.featuresDataResponse) throw new Error('The robot sent no feature flags')
        applyFeatures(reply.featuresDataResponse)
    }

    let loading = $state(refreshFeatures())

    const rows = $derived(
        $connectionFeatures && settings ? sensorRows($connectionFeatures, settings) : []
    )
    const problem = $derived(settings && form ? formProblem(settings, form) : null)

    const save = async () => {
        if (!settings || !form) return
        const saved = settingsFromForm($state.snapshot(settings), $state.snapshot(form))
        try {
            await peripherals.save(() => robotRequest({ peripheralSettings: saved }))
            notifications.success('Sensor settings saved', 3000)
        } catch (error) {
            notifications.error(`Saving sensor settings failed: ${(error as Error).message}`, 5000)
            return
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
        <LoadError {error} retry={() => (loading = refreshFeatures())} />
    {/await}

    {#if !settings && peripherals.problem}
        <div role="alert" class="alert alert-error alert-soft">
            <span>{peripherals.problem}</span>
        </div>
    {/if}
    {#if peripherals.changedOnRobot}
        <p class="text-warning text-sm" data-testid="changed-on-robot">
            Changed on the robot meanwhile.
            <button class="link" type="button" onclick={() => peripherals.reset()}>
                Show the robot's
            </button>
        </p>
    {/if}

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

        {#if $connectionFeatures?.espnowControllerSupported}
            <div class="divider my-1">Handheld controller</div>
            <label class="label gap-2">
                <input
                    type="checkbox"
                    class="toggle toggle-sm toggle-primary"
                    bind:checked={form.espnowController}
                />
                Listen for the ESP-NOW controller
            </label>
            <p class="text-xs opacity-70">
                The robot obeys the first controller it hears after this is switched on.
            </p>
        {/if}

        {#if form.leftSonar && form.rightSonar}
            <div class="divider my-1">HC-SR04 sonars</div>
            {#each [{ name: 'Left', sonar: form.leftSonar }, { name: 'Right', sonar: form.rightSonar }] as side (side.name)}
                <div class="flex flex-wrap items-center gap-4">
                    <label class="label w-28 gap-2">
                        <input
                            type="checkbox"
                            class="toggle toggle-sm toggle-primary"
                            aria-label="Use the {side.name.toLowerCase()} sonar"
                            bind:checked={side.sonar.enabled}
                        />
                        {side.name}
                    </label>
                    <label class="input input-sm w-36">
                        Trigger
                        <input
                            type="number"
                            min="0"
                            aria-label="{side.name} sonar trigger pin"
                            disabled={!side.sonar.enabled}
                            bind:value={side.sonar.triggerPin}
                        />
                    </label>
                    <label class="input input-sm w-36">
                        Echo
                        <input
                            type="number"
                            min="0"
                            aria-label="{side.name} sonar echo pin"
                            disabled={!side.sonar.enabled}
                            bind:value={side.sonar.echoPin}
                        />
                    </label>
                </div>
            {/each}
            <p class="text-xs opacity-70">
                The echo pin carries 5 V: wire it through a divider, such as 1 kOhm from the echo to
                the pin and 2 kOhm from the pin to ground. The sensors ping in turn, ten times a
                second each.
            </p>
        {/if}

        {#if form.powerSensor}
            <div class="divider my-1">INA219 power sensor</div>
            <div class="flex flex-wrap items-center gap-4">
                <label class="select select-sm w-36">
                    <span class="label">Address</span>
                    <select aria-label="Power sensor address" bind:value={form.powerSensor.address}>
                        {#each POWER_ADDRESSES as address (address)}
                            <option value={address}>0x{address.toString(16).toUpperCase()}</option>
                        {/each}
                    </select>
                </label>
                <label class="input input-sm w-40">
                    Shunt
                    <input
                        type="number"
                        min="0"
                        step="0.001"
                        aria-label="Shunt resistance in ohms"
                        bind:value={form.powerSensor.shuntOhms}
                    />
                    Ohm
                </label>
                <label class="input input-sm w-40">
                    Max
                    <input
                        type="number"
                        min="0"
                        step="0.1"
                        aria-label="Maximum current in amperes"
                        bind:value={form.powerSensor.maxCurrentA}
                    />
                    A
                </label>
            </div>
            <div class="flex flex-wrap items-center gap-4">
                <label class="input input-sm w-44">
                    Cut-off
                    <input
                        type="number"
                        min="0"
                        max="26"
                        step="0.1"
                        aria-label="Cut-off voltage"
                        bind:value={form.powerSensor.cutoffVoltage}
                    />
                    V
                </label>
                <label class="input input-sm w-44">
                    Stall
                    <input
                        type="number"
                        min="0"
                        step="0.1"
                        aria-label="Stall current in amperes"
                        bind:value={form.powerSensor.stallCurrentA}
                    />
                    A
                </label>
                <label class="input input-sm w-44">
                    for
                    <input
                        type="number"
                        min="0"
                        step="100"
                        aria-label="Stall time in milliseconds"
                        bind:value={form.powerSensor.stallTimeMs}
                    />
                    ms
                </label>
            </div>
            <p class="text-xs opacity-70">
                0x40 is the servo board's: tie the INA219's A0 high for 0x41. The maximum current
                sets the resolution and may put at most 0.32 V across the shunt. Under the cut-off
                for 2 s the robot deactivates, and stays so until the rail rises 0.2 V above it; 0
                turns it off. A current over the stall current for the stall time is only reported;
                0 turns it off.
            </p>
        {/if}

        {#if problem}
            <div role="alert" class="alert alert-error alert-soft">{problem}; not saved.</div>
        {/if}
        <div>
            <button
                class="btn btn-primary btn-sm"
                onclick={save}
                disabled={peripherals.saving || !!problem}
            >
                {#if peripherals.saving}
                    <span class="loading loading-spinner loading-xs"></span>
                {/if}
                Save
            </button>
        </div>
    {/if}
</SettingsCard>
