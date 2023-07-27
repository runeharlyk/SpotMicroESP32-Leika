<script lang="ts">
	import Stream from '../Views/Stream.svelte';
	import Controls from '../components/Controls.svelte';
    import ModelView from '../components/Model/ModelView.svelte';
	import { emulateModel } from '../lib/store';
</script>

<div class="flex justify-center items-center w-full h-full">
    {#if $emulateModel}
        <ModelView />
    {:else}
        <Stream />
    {/if}
	<Controls />
</div>
