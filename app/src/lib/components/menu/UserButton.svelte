<script lang="ts">
	import { user } from '$lib/stores';
	import { useFeatureFlags } from "$lib/stores";
	import { Avatar, Logout } from '../icons';

    const features = useFeatureFlags();
</script>

{#if $features.security}
	<div class="flex items-center">
		<Avatar class="h-8 w-8" />
		<span class="flex-grow px-4 text-xl font-bold">{$user.username}</span>
		<button class="btn btn-ghost" on:click={user.invalidate}>
			<Logout class="h-8 w-8 rotate-180" />
		</button>
	</div>
{/if}
