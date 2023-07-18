<script lang="ts">
	import nipplejs from 'nipplejs';
	import { onMount } from 'svelte';
	import { throttler } from '../lib/throttle';
	import { socket } from '../lib/socket';
	import { emulateModel, input, outControllerData } from '../lib/store';

	let throttle = new throttler();
	let left: nipplejs.JoystickManager;
	let right: nipplejs.JoystickManager;

	let throttle_timing = 40;

	let mode = 'rest'; // 'rest' | 'stand' | 'stand+' | 'walk'

	let data = new Uint8Array(6);

	onMount(() => {
		left = nipplejs.create({
			zone: document.getElementById('left') as HTMLElement,
			color: 'grey',
			dynamicPage: true,
			mode: 'static',
			restOpacity: 0.3
		});

		left.on('move', (evt, data) => {
            input.update(o => {o.left = data.vector; return o;})
			throttle.throttle(updateData, throttle_timing);
		});

		left.on('end', (evt, data) => {
            input.update(o => {o.left = { x: 0, y: 0 }; return o;})
			throttle.throttle(updateData, throttle_timing);
		});

		right = nipplejs.create({
			zone: document.getElementById('right') as HTMLElement,
			color: 'grey',
			dynamicPage: true,
			mode: 'static',
			restOpacity: 0.3
		});

		right.on('move', (evt, data) => {
            input.update(o => {o.right = data.vector; return o;})
			throttle.throttle(updateData, throttle_timing);
		});

		right.on('end', (evt, data) => {
            input.update(o => {o.right = { x: 0, y: 0 }; return o;})
			throttle.throttle(updateData, throttle_timing);
		});
	});

	const updateData = () => {
		data[0] = 0;
		data[1] = $input.left.x * 127 + 128;
		data[2] = $input.left.y * 127 + 128;
		data[3] = $input.right.x * 127 + 128;
		data[4] = $input.right.y * 127 + 128;
		data[5] = $input.height;
		data[6] = $input.speed;

        outControllerData.set(data)

        if(!$emulateModel) $socket.send(data);
	};

</script>

<div class="absolute top-0 left-0 w-screen h-screen">
	<div class="absolute top-0 left-0 h-full w-full flex portrait:hidden">
		<div id="left" class="flex w-60 items-center justify-end" />
		<div class="flex-1" />
		<div id="right" class="flex w-60 items-center" />
	</div>
</div>
