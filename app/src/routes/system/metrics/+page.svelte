<script lang="ts">
	import SystemMetrics from './SystemMetrics.svelte';
	import { page } from '$app/stores';
	import { goto } from '$app/navigation';

	if (!$page.data.features.analytics) {
		goto('/');
	}
</script>

<div class="mx-0 my-1 flex flex-col space-y-4 sm:mx-8 sm:my-8">
	<SystemMetrics />
</div>
