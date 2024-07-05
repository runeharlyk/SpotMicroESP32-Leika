<script lang="ts">
	import { user } from '$lib/stores/user';
	import { onDestroy } from 'svelte';

	const ws_token = `?access_token=${$user.bearer_token}`

    let source = "/api/camera/stream"+ ws_token;

	onDestroy(() => {
		source = '#';
	});
</script>

<div class="w-full h-full">
	<img
		src={source}
		class="absolute object-cover blur-3xl w-full h-full -z-10"
		alt="Live stream is down"
	/>
	<img src={source} class="object-contain w-full h-full" alt="Live stream is down" />
</div>
