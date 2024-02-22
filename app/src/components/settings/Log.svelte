<script lang="ts">
	import socketService from "$lib/services/socket-service";
    import { onMount } from 'svelte'

    let isConnected = socketService.isConnected
    let log = socketService.log
    
    onMount(() => {
        if ($isConnected) {
            const message = JSON.stringify({type: 'system/logs'})
            socketService.send(message)
        }
    })

</script>

<div class="w-full h-full">
    {#each $log as entry}
        <div>{entry}</div>
    {/each}
</div>