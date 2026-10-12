<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { SignalDistance } from '$lib/components/icons'
    import { sonar } from '$lib/stores/sonar'
    import { sonarText } from '$lib/sonar'
    import { onDestroy, onMount } from 'svelte'

    const latest = $derived($sonar.at(-1))

    onMount(() => sonar.listen())
    onDestroy(() => sonar.stop())
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <SignalDistance class="mr-2 h-6 w-6 flex-shrink-0 self-end" />
    {/snippet}
    {#snippet title()}
        <span>Sonar</span>
    {/snippet}

    <div class="stats stats-horizontal shadow">
        {#each [{ label: 'Left', reading: latest?.left }, { label: 'Right', reading: latest?.right }] as side (side.label)}
            <div class="stat">
                <div class="stat-title">{side.label}</div>
                <div class="stat-value text-2xl" data-testid="sonar-{side.label.toLowerCase()}">
                    {latest ? sonarText(side.reading) : 'Waiting'}
                </div>
            </div>
        {/each}
    </div>
</SettingsCard>
