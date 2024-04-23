<script lang="ts">
	import Controls from './Controls.svelte';
	import { socket } from '$lib/stores';
</script>
<div>
    {#if $socket}
        <Controls />
        <slot/>
    {:else}
        <div class="flex justify-center items-center">
            <h2>Waiting for connection</h2>
        </div>
    {/if}
</div>
