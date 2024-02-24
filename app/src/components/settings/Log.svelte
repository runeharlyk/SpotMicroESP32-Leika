<script lang="ts">
	import socketService from '$lib/services/socket-service';
	import { isConnected, logs } from '$lib/stores';
	import { onMount } from 'svelte';

	onMount(() => {
		if ($isConnected) {
			const message = JSON.stringify({ type: 'system/logs' });
			socketService.send(message);
		}
	});
</script>

<div class="w-full h-full">
	{#each $logs as entry}
		<div>{entry}</div>
	{/each}
</div>
