<script lang="ts">
	import SettingsCard from "$lib/components/SettingsCard.svelte";
	import CameraSetting from './CameraSetting.svelte';
	import Stream from '$lib/components/Stream.svelte';
	import { Camera } from "$lib/components/icons";
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
		<Camera  class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
	{/snippet}
    {#snippet title()}
		<span >Camera</span>
	{/snippet}
    <Stream />
    <CameraSetting />
</SettingsCard>