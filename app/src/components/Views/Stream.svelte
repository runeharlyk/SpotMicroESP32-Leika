<script lang="ts">
	import { onDestroy } from 'svelte';
	import location from '../../lib/location';

	let videoStream = `//${location}/stream`;

	onDestroy(() => {
		videoStream = '#';
	});
</script>

<img src={videoStream} class="object-contain w-full h-full" alt="Live stream is down" />
