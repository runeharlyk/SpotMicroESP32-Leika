<script>
    import FileIcon from '~icons/mdi/file';
    import { createEventDispatcher } from 'svelte';
    
	export let name;
    
    const dispatch = createEventDispatcher();

    const updateSelected = async () => {
        dispatch('selected', { name });
    }
</script>

<!-- svelte-ignore a11y-interactive-supports-focus -->
<!-- svelte-ignore a11y-click-events-have-key-events -->
<span role="button" class="flex pl-4 gap-2 items-center" on:click={updateSelected}>
    <FileIcon/>{name}
</span>
