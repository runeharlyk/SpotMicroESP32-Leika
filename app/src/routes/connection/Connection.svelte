<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { WiFi } from '$lib/components/icons'
    import { apiLocation, socket } from '$lib/stores'

    const update = () => {
        const ws = $apiLocation ? $apiLocation : window.location.host
        socket.init(`ws://${ws}/api/ws`)
    }
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <WiFi class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    {/snippet}
    {#snippet title()}
        <span>Connection</span>
    {/snippet}

    <div class="flex">
        <label class="label w-32" for="server">Address:</label>
        <input class="input" bind:value={$apiLocation} />
    </div>

    <button class="btn btn-primary" onclick={update}>Update</button>
</SettingsCard>
