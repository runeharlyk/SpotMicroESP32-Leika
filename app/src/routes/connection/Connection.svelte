<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte';
    import { WiFi } from '$lib/components/icons';
    import { location, socket, useFeatureFlags } from '$lib/stores';

    const features = useFeatureFlags();

    const update = () => {
        const ws = $location ? $location : window.location.host;
        socket.init(`ws://${ws}/api/ws/events`);
    };
</script>

<SettingsCard collapsible={false}>
    <WiFi slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    <span slot="title">Connection</span>

    <div class="flex">
        <label class="label w-32" for="server">Address:</label>
        <input class="input" bind:value={$location} />
    </div>

    <button class="btn btn-primary" on:click={update}>Update</button>
</SettingsCard>
