<script lang="ts">
    import type { Snippet } from 'svelte'
    import { page } from '$app/state'
    import { apiLocation, hasRobot } from '$lib/stores'
    import { socket } from '$lib/stores/socket'
    import { Warning } from './icons'

    const { children }: { children: Snippet } = $props()

    const transport = socket.transport
    // Without a robot the socket never opens, so requests would only time out.
    const robot = $derived(hasRobot(page.url, $apiLocation, $transport))
</script>

{#if robot}
    {@render children()}
{:else}
    <div role="alert" class="alert alert-warning alert-soft w-full max-w-2xl self-center">
        <Warning class="h-6 w-6 shrink-0" />
        <span>
            Add the robot's address on the home page, or connect over Bluetooth or USB, to change
            these settings.
        </span>
    </div>
{/if}
