<script>
	import SettingsCard from "$lib/components/SettingsCard.svelte";
	import Spinner from "$lib/components/Spinner.svelte";
    import FolderIcon from '~icons/mdi/folder-outline';
	import Folder from "./Folder.svelte";
	import { api } from "$lib/api";

    const getFiles = async () => {
        const result = await api.get('/api/files/list')
        if (result.isOk()) {
            return result.inner;
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