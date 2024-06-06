<script lang="ts">
	import nipplejs from 'nipplejs';
	import { onMount } from 'svelte';
	import { capitalize, throttler, toInt8 } from '$lib/utilities';
	import { input, outControllerData, mode, modes, type Modes, ModesEnum, socket } from '$lib/stores';
	import type { vector } from '$lib/models';

	let throttle = new throttler();
	let left: nipplejs.JoystickManager;
	let right: nipplejs.JoystickManager;

	let throttle_timing = 40;
	let data = new Array(7);

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

    const handleRange = (event:Event, key: 'speed' | 'height') => {
        const value:number = event.target?.value
        
        input.update((inputData) => {
			inputData[key] = value;
			return inputData;
		});
        throttle.throttle(updateData, throttle_timing);
    }

	const changeMode = (modeValue: Modes) => {
		mode.set(modes.indexOf(modeValue));
	};
</script>

<div class="absolute top-0 left-0 w-screen h-screen">
	<div class="absolute top-0 left-0 h-full w-full flex portrait:hidden">
		<div id="left" class="flex w-60 items-center justify-end" />
		<div class="flex-1" />
		<div id="right" class="flex w-60 items-center" />
	</div>
    <div class="absolute bottom-0 right-0 p-4 z-10 gap-2 flex-col hidden lg:flex">
        <div class="flex justify-center w-full">
            <kbd class="kbd">W</kbd>
        </div>
        <div class="flex justify-center gap-2 w-full">
            <kbd class="kbd">A</kbd>
            <kbd class="kbd">S</kbd>
            <kbd class="kbd">D</kbd>
        </div>
        <div class="flex justify-center w-full">
        </div>
    </div>
	<div class="absolute bottom-0 z-10 p-4 gap-4 flex items-end">
		{#each modes as modeValue}
            <button class="btn btn-outline" class:btn-active={$mode === modes.indexOf(modeValue)} on:click={() => changeMode(modeValue)}>
                {capitalize(modeValue)}
            </button>
		{/each}
        <div>
            {#if $mode === ModesEnum.Walk}
                <label for="speed">Speed</label>
                <input type="range" name="speed" min="0" max="100" on:input={(e) => handleRange(e, 'speed')} class="range range-sm" />
            {/if}
            <label for="height">Height</label>
            <input type="range" name="height" min="0" max="100" on:input={(e) => handleRange(e, 'height')} class="range range-sm" />
        </div>
	</div>
</div>

<svelte:window on:keyup={handleKeyup} on:keydown={handleKeyup} />
