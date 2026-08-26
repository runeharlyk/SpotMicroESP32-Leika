<script lang="ts">
    import { goto } from '$app/navigation'
    import Visualization from '$lib/components/Visualization.svelte'
    import { socket } from '$lib/stores'
    import { onMount } from 'svelte'
    import { resolve } from '$app/paths'

    onMount(() => {
        socket.subscribe(isConnected => {
            if (isConnected) {
                goto(resolve('/controller'))
            }
        })
    })
</script>

<div class="hero bg-base-100 min-h-screen">
    <div class="card bg-base-200 md:card-side border-base-300 items-center border shadow-xl">
        <div class="size-56 shrink-0 md:size-64">
            <Visualization defaultColor={null} orbit panel={false} ground={false} />
        </div>
        <div class="card-body w-80 items-start gap-4">
            <h2 class="card-title text-2xl">No robot connected</h2>
            <p class="text-base-content/70">
                Point the controller at a robot on your network to start driving it.
            </p>
            <a
                class="btn btn-primary mt-2 w-full"
                href={resolve($socket ? '/controller' : '/connection')}
            >
                {$socket ? 'Open controller' : 'Connect to a robot'}
            </a>
        </div>
    </div>
</div>
