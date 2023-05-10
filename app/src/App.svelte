<script lang="ts">
	import { onMount } from 'svelte';
	import Topbar from './components/Topbar.svelte';
	import { connect } from './lib/socket';
	import Stream from './components/Views/Stream.svelte';
	import Controls from './components/Controls.svelte';

	onMount(() => {
		connect('ws://leika.local');
	});
</script>

<main class="w-screen h-screen">
	<Topbar />
	<div class="flex justify-center items-center w-full h-full">
		<Stream />
		<Controls />
	</div>
</main>
