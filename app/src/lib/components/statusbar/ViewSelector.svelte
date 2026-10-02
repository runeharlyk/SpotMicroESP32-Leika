<script lang="ts">
    import { page } from '$app/state'
    import { apiLocation, hasRobot } from '$lib/stores'
    import { socket } from '$lib/stores/socket'
    import { offlineView, selectedView, views } from '$lib/stores/application'
    import Selector from '../widget/Selector.svelte'

    const transport = socket.transport
    const robot = $derived(hasRobot(page.url, $apiLocation, $transport))
    const choice = $derived(robot ? selectedView : offlineView)
</script>

<Selector
    bind:selectedOption={() => (robot ? $selectedView : $offlineView), name => choice.set(name)}
    options={$views.map(v => v.name)}
/>
