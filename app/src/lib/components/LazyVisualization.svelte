<script lang="ts">
    import { HAS_3D_VIEW } from '$lib/build-flags'

    interface Props {
        defaultColor?: string | null
        orbit?: boolean
        panel?: boolean
        debug?: boolean
        ground?: boolean
    }

    const props: Props = $props()
</script>

{#if HAS_3D_VIEW}
    {#await import('./Visualization.svelte') then { default: Visualization }}
        <Visualization {...props} />
    {/await}
{:else}
    <div class="flex h-full w-full items-center justify-center p-4 text-center text-sm opacity-60">
        The 3D view is part of the web app, not the controller built into the robot.
    </div>
{/if}
