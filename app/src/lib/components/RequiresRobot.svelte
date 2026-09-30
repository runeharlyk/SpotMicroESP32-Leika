<script lang="ts">
    import type { Snippet } from 'svelte'
    import { page } from '$app/state'
    import { apiLocation, canReachRobot } from '$lib/stores'
    import { socket } from '$lib/stores/socket'
    import { Warning } from './icons'

    const { children }: { children: Snippet } = $props()

    const transport = socket.transport
    // Without an address the socket never opens, so requests would only time out; Bluetooth
    // needs no address.
    const hasRobot = $derived(canReachRobot(page.url, $apiLocation) || $transport === 'bluetooth')
</script>

{#if hasRobot}
    {@render children()}
{:else}
    <div role="alert" class="alert alert-warning alert-soft w-full max-w-2xl self-center">
        <Warning class="h-6 w-6 shrink-0" />
        <span>
            Add the robot's address on the home page, or connect over Bluetooth, to change these
            settings.
        </span>
    </div>
{/if}
