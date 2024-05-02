<script lang="ts">
	import Controls from './Controls.svelte';
	import { socket } from '$lib/stores';
	import Spinner from '$lib/components/Spinner.svelte';
</script>
<div>
    {#if !$socket}
    <div class="flex flex-col h-full justify-center items-center">
        <Spinner/>
            <h2>Waiting for connection</h2>
        </div>
    {:else}
        <Controls />
        <slot/>
    {/if}
</div>
