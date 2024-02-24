<script lang="ts">
	import nipplejs from 'nipplejs';
	import { onMount } from 'svelte';
	import { capitalize, throttler, toInt8 } from '$lib/utilities';
	import { input, outControllerData, mode, modes } from '$lib/stores';
	import type { vector } from '$lib/models';

	let throttle = new throttler();
	let left: nipplejs.JoystickManager;
	let right: nipplejs.JoystickManager;

	let throttle_timing = 40;
	let data = new Int8Array(6);

	onMount(() => {
		left = nipplejs.create({
			zone: document.getElementById('left') as HTMLElement,
			color: 'grey',
			dynamicPage: true,
			mode: 'static',
			restOpacity: 0.3
		});

		right = nipplejs.create({
			zone: document.getElementById('right') as HTMLElement,
			color: 'grey',
			dynamicPage: true,
			mode: 'static',
			restOpacity: 0.3
		});

		left.on('move', (_, data) => handleJoyMove('left', data.vector));
		left.on('end', (_, __) => handleJoyMove('left', { x: 0, y: 0 }));
		right.on('move', (_, data) => handleJoyMove('right', data.vector));
		right.on('end', (_, __) => handleJoyMove('right', { x: 0, y: 0 }));
	});

	const handleJoyMove = (key: 'left' | 'right', data: vector) => {
		input.update((inputData) => {
			inputData[key] = data;
			return inputData;
		});
		throttle.throttle(updateData, throttle_timing);
	};

	const updateData = () => {
		data[0] = 0;
		data[1] = toInt8($input.left.x, -1, 1);
		data[2] = toInt8($input.left.y, -1, 1);
		data[3] = toInt8($input.right.x, -1, 1);
		data[4] = toInt8($input.right.y, -1, 1);
		data[5] = toInt8($input.height, 0, 100);
		data[6] = toInt8($input.speed, 0, 100);

		outControllerData.set(data);
	};

	const handleKeyup = (event: KeyboardEvent) => {
		const down = event.type === 'keydown';
		input.update((data) => {
			if (event.key === 'w') data.left.y = down ? -1 : 0;
			if (event.key === 'a') data.left.x = down ? -1 : 0;
			if (event.key === 's') data.left.y = down ? 1 : 0;
			if (event.key === 'd') data.left.x = down ? 1 : 0;
			return data;
		});
		throttle.throttle(updateData, throttle_timing);
	};

	const changeMode = (modeValue: Modes) => {
		mode.set(modeValue);
	};
</script>

<div class="absolute top-0 left-0 w-screen h-screen">
	<div class="absolute top-0 left-0 h-full w-full flex portrait:hidden">
		<div id="left" class="flex w-60 items-center justify-end" />
		<div class="flex-1" />
		<div id="right" class="flex w-60 items-center" />
	</div>
	<div class="absolute bottom-0 z-10 p-4 gap-4 flex">
		{#each modes as modeValue}
			<button
				on:click={() => changeMode(modeValue)}
				class="rounded-md outline outline-2 text-zinc-200 outline-zinc-600 p-2">
                {capitalize(modeValue)}
            </button>
		{/each}
	</div>
</div>

<svelte:window on:keyup={handleKeyup} on:keydown={handleKeyup} />
