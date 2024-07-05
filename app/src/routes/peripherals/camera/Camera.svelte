<script lang="ts">
	import SettingsCard from "$lib/components/SettingsCard.svelte";
    import Camera from '~icons/mdi/camera-outline'
    import Record from '~icons/mdi/radio-button-unchecked'
	import CameraSetting from './CameraSetting.svelte';
	import Stream from '$lib/components/Stream.svelte';
</script>

<SettingsCard collapsible={false}>
    <Camera slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    <span slot="title">Camera</span>
    <Stream />
    <CameraSetting />
</SettingsCard>