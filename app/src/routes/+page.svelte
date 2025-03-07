<script lang="ts">
  import { goto } from '$app/navigation'
  import Visualization from '$lib/components/Visualization.svelte'
  import { socket } from '$lib/stores'
  import { onMount } from 'svelte'

  onMount(() => {
    socket.subscribe(isConnected => {
      if (isConnected) {
        goto('/controller')
      }
    })
  })
</script>

<div class="w-full h-full flex justify-center items-center">
  <div class="h-full flex flex-col">
    <div class="grow-3 w-80 relative">
      <Visualization sky={false} orbit panel={false} ground={false} zoom={8} />
      <div class="absolute bottom-0 w-full h-40 bg-gradient-to-t from-base-100 to-transparent">
      </div>
    </div>
    <div class="grow-3 flex justify-center">
      <a class="btn btn-primary rounded-full" href={$socket ? '/controller' : '/connection'}>
        Add Robot Dog
      </a>
    </div>
  </div>
</div>
