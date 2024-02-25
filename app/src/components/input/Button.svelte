<script lang="ts">
    export let active = false
</script>

<button
    on:click
    class={$$restProps.class + ' rounded-md outline outline-2 text-zinc-200 outline-zinc-600 p-2' + 
        (active ? ' bg-zinc-600' : '')}
>
<slot/>
</button>