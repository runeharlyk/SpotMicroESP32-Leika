<script>
	import SettingsCard from "$lib/components/SettingsCard.svelte";
	import Spinner from "$lib/components/Spinner.svelte";
    import FolderIcon from '~icons/mdi/folder-outline';
	import Folder from "./Folder.svelte";

    const getFiles = async () => {
        const response = await fetch('/api/files/list');
        if (response.ok) {
            return response.json();
        }
    };
</script>
<SettingsCard collapsible={false}>
    <FolderIcon slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
	<span slot="title">File System</span>
    <div class="w-full overflow-x-auto">
		{#await getFiles()}
			<Spinner />
		{:then files}
            <Folder name="/" files={files.root} expanded />
        {/await}
    </div>
</SettingsCard>