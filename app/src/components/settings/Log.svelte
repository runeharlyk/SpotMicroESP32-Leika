<script lang="ts">
    import { socket, isConnected, log } from "../../lib/socket";
    import { onMount } from 'svelte'
    
    onMount(() => {
        if ($isConnected) {
            const message = JSON.stringify({type: 'system/logs'})
            $socket.send(message)
        }
    })

</script>

<div class="w-full h-full">
    {#each $log as entry}
        <div>{entry}</div>
    {/each}
</div>