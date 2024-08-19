<script lang="ts">
	import File from './File.svelte';
	import { createEventDispatcher } from 'svelte';
	import { FolderIcon, FolderOpenOutline } from '$lib/components/icons';

	export let expanded = false;
	export let name;
	export let files;

	function toggle() {
		expanded = !expanded;
	}

    const dispatch = createEventDispatcher();

    const updateSelected = async (event:any) => {
        dispatch('selected', { name:event.detail.name });
    }
</script>

<button class="flex pl-2" on:click={toggle}>
    {#if expanded}
        <FolderOpenOutline class="w-6 h-6" />
    {:else}
        <FolderIcon class="w-6 h-6" />
    {/if}
    {name}
</button>

{#if expanded}
    <ul class="ml-5 border-l border-slate-600">
        {#each Object.entries(files) as [name, content]}
            <li class="p-1">
                {#if typeof content == 'object'}
					<svelte:self {name} files={content} on:selected={updateSelected} />
				{:else}
					<File {name} on:selected={updateSelected}/>
				{/if}
			</li>
		{/each}
	</ul>
{/if}
