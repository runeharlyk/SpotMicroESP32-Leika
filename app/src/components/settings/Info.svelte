<script lang="ts">
    import { socket, isConnected, systemInfo } from "../../lib/socket";
    import { onMount } from 'svelte'
	import { humanFileSize } from "../../lib/utils";
    
    onMount(() => {
        if ($isConnected) {
            const message = JSON.stringify({type: 'system/info'})
            $socket.send(message)
        }
    })

</script>
<div class="w-full h-full">
    <div class="w-1/3">
        {#each Object.entries($systemInfo ?? {}) as entry}
           <div class="flex gap-8">
            <div class="w-32">{entry[0]}:</div>
            {#if entry[0].includes("Size") || entry[0].includes("Free") || entry[0].includes("Min")}
                <div>{humanFileSize(entry[1])}</div>
            {:else}
                <div>{entry[1]}</div>
            {/if}
        </div>
        {/each}
    </div>
</div>