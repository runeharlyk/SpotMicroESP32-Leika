<script>
	import File from './File.svelte';
    import FolderIcon from '~icons/mdi/folder-outline';
    import FolderOpenOutline from '~icons/mdi/folder-open-outline';

	export let expanded = false;
	export let name;
	export let files;

	function toggle() {
		expanded = !expanded;
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
					<svelte:self {name} files={content} />
				{:else}
					<File {name} />
				{/if}
			</li>
		{/each}
	</ul>
{/if}
