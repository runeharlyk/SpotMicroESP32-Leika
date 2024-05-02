<script>
    import FileIcon from '~icons/mdi/file';
	export let name;
	// $: type = name.slice(name.lastIndexOf('.') + 1);
</script>

<span class="flex pl-4 gap-2 items-center"><FileIcon/>{name}</span>
