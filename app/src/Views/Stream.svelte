<script lang="ts">
	import { onDestroy } from 'svelte';
	import location from '../lib/location';

	let videoStream = `//${location}/api/stream`;

	onDestroy(() => {
		videoStream = '#';
	});
</script>

<div class="w-full h-full">
	<img
		src={videoStream}
		class="absolute object-cover blur-3xl w-full h-full -z-10"
		alt="Live stream is down"
	/>
	<img src={videoStream} class="object-contain w-full h-full" alt="Live stream is down" />
</div>
