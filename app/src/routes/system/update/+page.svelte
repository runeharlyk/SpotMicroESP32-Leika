<script lang="ts">
	import UploadFirmware from './UploadFirmware.svelte';
	import GithubFirmwareManager from './GithubFirmwareManager.svelte';
	import { user } from '$lib/stores/user';
	import { useFeatureFlags } from '$lib/stores';

    const features = useFeatureFlags();
</script>

<div class="mx-0 my-1 flex flex-col space-y-4 sm:mx-8 sm:my-8">
	{#if $features.download_firmware && (!$features.security || $user.admin)}
		<GithubFirmwareManager />
	{/if}

	{#if $features.upload_firmware && (!$features.security || $user.admin)}
		<UploadFirmware />
	{/if}
</div>
