<script lang="ts">
	import Folder from './Folder.svelte';
	import File from './File.svelte';
	import { createEventDispatcher } from 'svelte';
	import { FolderIcon, FolderOpenOutline } from '$lib/components/icons';

	interface Props {
		expanded?: boolean;
		name: any;
		files: any;
	}

	let { expanded = $bindable(false), name, files }: Props = $props();

	function toggle() {
		expanded = !expanded;
	}

    const dispatch = createEventDispatcher();

    const updateSelected = async (event:any) => {
        dispatch('selected', { name:event.detail.name });
    }
</script>

<button class="flex pl-2" onclick={toggle}>
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
					<Folder {name} files={content} on:selected={updateSelected} />
				{:else}
					<File {name} on:selected={updateSelected}/>
				{/if}
			</li>
		{/each}
	</ul>
{/if}
