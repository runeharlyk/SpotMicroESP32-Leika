<script lang="ts">
	import Controls from './Controls.svelte';
	import { isConnected } from '$lib/stores';
</script>
<div>
    {#if $isConnected}
        <Controls />
        <slot/>
    {:else}
        <div class="flex justify-center items-center">
            <h2>Waiting for connection</h2>
        </div>
    {/if}
</div>
