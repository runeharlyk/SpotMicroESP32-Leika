<script lang="ts">
	import nipplejs from 'nipplejs';
	import { onMount } from 'svelte';
	import { isPortrait } from '../lib/UseOrientation';

	onMount(() => {
		let left = nipplejs.create({
			zone: document.getElementById('left') as HTMLElement,
			color: 'grey',
			dynamicPage: true,
			mode: 'static'
		});
		let right = nipplejs.create({
			zone: document.getElementById('right') as HTMLElement,
			color: 'grey',
			dynamicPage: true,
			mode: 'static'
		});
	});
</script>

<div class="absolute top-0 left-0 w-screen h-screen">
	<div class="absolute top-0 left-0 h-full w-full flex {isPortrait ? 'hide' : ''}">
		<div id="left" class="flex w-60 items-center justify-end" />
		<div class="flex-1" />
		<div id="right" class="flex w-60 items-center" />
	</div>
</div>
