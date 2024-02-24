<script lang="ts">
	import { socketService } from '$lib/services';
	import { isConnected, settings } from '$lib/stores';
	import { onMount } from 'svelte';

	onMount(() => {
		if ($isConnected) {
			const message = JSON.stringify({ type: 'system/settings' });
			socketService.send(message);
		}
	});
</script>

<div class="w-full h-full">
	<div>
		{#each Object.entries($settings) as entry}
			<div class="flex gap-8">
				<div class="w-32">{entry[0]}:</div>
				<div>{entry[1]}</div>
			</div>
		{/each}
	</div>
</div>
