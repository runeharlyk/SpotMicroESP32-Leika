<script lang="ts">
	import Stream from '$components/Views/Stream.svelte';
	import Model from '$components/Views/Model.svelte';
	import Controls from '$components/Controls.svelte';
	import { emulateModel } from '$lib/store';
</script>

<div class="flex justify-center items-center w-full h-full">
	{#if $emulateModel}
		<Model />
	{:else}
		<Stream />
	{/if}
	<Controls />
</div>
