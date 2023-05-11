<script lang="ts">
	import nipplejs from 'nipplejs';
	import { onMount } from 'svelte';
	import { throttler } from '../lib/throttle';

	let throttle = new throttler();
	let left: nipplejs.JoystickManager;
	let right: nipplejs.JoystickManager;

	let throttle_timing = 50;

	let left_vector = { x: 0, y: 0 };
	let right_vector = { x: 0, y: 0 };
	let height = 0; // -50 to 50
	let speed = 0;
	let mode = 'rest'; // 'rest' | 'stand' | 'stand+' | 'walk'
	let stream_rotation = 0;
	let temp = 0;

	onMount(() => {
		left = nipplejs.create({
			zone: document.getElementById('left') as HTMLElement,
			color: 'grey',
			dynamicPage: true,
			mode: 'static'
		});

		left.on('move', (evt, data) => {
			left_vector = data.vector;
			throttle.throttle(updateData, throttle_timing);
		});

		left.on('end', (evt, data) => {
			left_vector = { x: 0, y: 0 };
			throttle.throttle(updateData, throttle_timing);
		});

		right = nipplejs.create({
			zone: document.getElementById('right') as HTMLElement,
			color: 'grey',
			dynamicPage: true,
			mode: 'static'
		});

		right.on('move', (evt, data) => {
			right_vector = data.vector;
			throttle.throttle(updateData, throttle_timing);
		});

		right.on('end', (evt, data) => {
			right_vector = { x: 0, y: 0 };
			throttle.throttle(updateData, throttle_timing);
		});
	});

	const updateData = () => {
		console.log(height, left_vector, right_vector);
	};

	const lerp = (start: number, end: number, amt: number) => {
		return (1 - amt) * start + amt * end;
	};
</script>

<div class="absolute top-0 left-0 w-screen h-screen">
	<div class="absolute top-0 left-0 h-full w-full flex portrait:hidden">
		<div id="left" class="flex w-60 items-center justify-end" />
		<div class="flex-1" />
		<div id="right" class="flex w-60 items-center" />
	</div>
</div>
