<script lang="ts">
    import { createEventDispatcher } from 'svelte'
    const dispatch = createEventDispatcher()

	export let value = 50;
	export let min = 0;
	export let max = 100;
	export let label = '';

    const dispatchValueInput = () => {
        dispatch('value', value)
    }
</script>

<div class="">
	<input
		id="range"
		type="range"
		{min}
		{max}
		bind:value
        on:change
        on:input={dispatchValueInput}
		class="w-32 h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer dark:bg-gray-700"
	/>
</div>
<label for="range" class="block mb-2 text-sm font-medium text-gray-900 dark:text-white">{label}</label
>

