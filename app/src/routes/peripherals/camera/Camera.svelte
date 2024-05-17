<script lang="ts">
    import { user } from '$lib/stores/user';
	import SettingsCard from "$lib/components/SettingsCard.svelte";
    import Camera from '~icons/mdi/camera-outline'
    import Record from '~icons/mdi/radio-button-unchecked'
	import CameraSetting from './CameraSetting.svelte';
	import { onDestroy } from 'svelte';

    const ws_token = `?access_token=${$user.bearer_token}`

    let source = "/api/camera/stream"+ ws_token;

    onDestroy(() => {
        source = ""
    })
</script>


<SettingsCard collapsible={false}>
    <Camera slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    <span slot="title">Camera</span>
    <img src={source} alt="Live-stream" class="w-full rounded-lg shadow-lg" />
    <CameraSetting />
</SettingsCard>