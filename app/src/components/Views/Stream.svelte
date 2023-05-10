<script lang="ts">
	import { onDestroy } from 'svelte';

	let videoStream = '//leika.local/stream';

	onDestroy(() => {
		videoStream = '#';
	});
</script>

<img src={videoStream} class="object-contain w-full h-full" alt="Live stream is down" />
