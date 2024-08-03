<script lang="ts">
	import Controls from './Controls.svelte';
	import { socket } from '$lib/stores';
	import Spinner from '$lib/components/Spinner.svelte';
</script>
<div class="select-none">
    {#if !$socket}
        <div class="absolute left-0 flex flex-col w-screen h-screen justify-center items-center backdrop-blur-sm z-10">
            <Spinner/>
            <h2>Waiting for connection</h2>
        </div>
    {/if}
    <Controls />
    <slot/>
</div>
