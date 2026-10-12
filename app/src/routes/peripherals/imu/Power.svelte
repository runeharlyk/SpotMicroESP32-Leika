<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { LightningBolt } from '$lib/components/icons'
    import { power } from '$lib/stores/power'
    import { amps, lowestVoltage, powerWarnings, volts, watts } from '$lib/power'
    import { onDestroy, onMount } from 'svelte'

    const latest = $derived($power.at(-1))
    const lowest = $derived(lowestVoltage($power))
    const warnings = $derived(powerWarnings(latest))
    const stats = $derived([
        { id: 'voltage', label: 'Voltage', text: latest && volts(latest.busVoltage) },
        { id: 'current', label: 'Current', text: latest && amps(latest.current) },
        { id: 'power', label: 'Power', text: latest && watts(latest.power) },
        {
            id: 'lowest',
            label: 'Lowest, last 10 s',
            text: lowest === undefined ? undefined : volts(lowest)
        }
    ])

    onMount(() => power.listen())
    onDestroy(() => power.stop())
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <LightningBolt class="mr-2 h-6 w-6 flex-shrink-0 self-end" />
    {/snippet}
    {#snippet title()}
        <span>Servo rail</span>
    {/snippet}

    <div class="stats stats-vertical shadow sm:stats-horizontal">
        {#each stats as stat (stat.id)}
            <div class="stat">
                <div class="stat-title">{stat.label}</div>
                <div class="stat-value text-2xl" data-testid="power-{stat.id}">
                    {stat.text ?? 'Waiting'}
                </div>
            </div>
        {/each}
    </div>
    {#each warnings as warning (warning)}
        <div role="alert" class="alert alert-warning alert-soft">{warning}</div>
    {/each}
</SettingsCard>
