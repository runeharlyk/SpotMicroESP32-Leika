<script lang="ts">
    import type { Servo } from "$lib/types/models";
	import { createEventDispatcher } from "svelte";
    export let servo: Servo;

    const dispatch = createEventDispatcher();

    const sweep = () => {
        dispatch('sweep', {channel: servo.channel});
    };
</script>

<div>
    <h2 class="text-lg">{ servo.name }</h2>
    <div class="flex gap-2 items-center">
        Is inverted <input type="checkbox" bind:checked={servo.inverted} class="toggle"/>
    </div>
    <div>
        Middle position <input type="number" bind:value={servo.center_angle} class="input input-bordered input-sm max-w-xs"/>
    </div>

    <div class="relative mb-6">
        <label for="labels-range-input" class="sr-only">Labels range</label>
        <input id="labels-range-input" type="range" bind:value={servo.angle} min="0" max="180" class="w-full h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer dark:bg-gray-700">
        <span class="text-sm text-gray-500 dark:text-gray-400 absolute start-0 -bottom-6">0</span>
        <span class="text-sm text-gray-500 dark:text-gray-400 absolute start-1/2 -translate-x-1/2 rtl:translate-x-1/2 -bottom-6">90</span>
        <span class="text-sm text-gray-500 dark:text-gray-400 absolute end-0 -bottom-6">180</span>
    </div>
    <button class="btn btn-neutral btn-sm" on:click={sweep}>Sweep range</button>
</div>