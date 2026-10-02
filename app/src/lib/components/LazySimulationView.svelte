<script lang="ts">
    import { HAS_3D_VIEW } from '$lib/build-flags'
</script>

{#if HAS_3D_VIEW}
    {#await import('./SimulationView.svelte') then { default: SimulationView }}
        <SimulationView />
    {/await}
{:else}
    <div class="flex h-full w-full items-center justify-center p-4 text-center text-sm opacity-60">
        The simulation is part of the web app, not the controller built into the robot.
    </div>
{/if}
