<script lang="ts">
	import { onMount } from 'svelte';
	import Topbar from './components/Topbar.svelte';
	import { connect } from './lib/socket';
	import Stream from './components/Views/Stream.svelte';
	import Controls from './components/Controls.svelte';
	import location from './lib/location';
	import Sidebar from './components/Sidebar.svelte';

	onMount(() => {
		connect(`ws://${location}`);
	});
</script>

<main class="w-screen h-screen">
	<Topbar />
	<Sidebar />
	<div class="flex justify-center items-center w-full h-full">
		<Stream />
		<Controls />
	</div>
</main>
