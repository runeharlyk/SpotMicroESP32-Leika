<script lang="ts">
	import { createBubbler } from 'svelte/legacy';

	const bubble = createBubbler();
	interface Props {
		options?: string[];
		selectedOption?: string;
		[key: string]: any
	}

	let { options = [], selectedOption = $bindable(''), ...rest }: Props = $props();
</script>

<select
	bind:value={selectedOption}
	onchange={bubble('change')}
	class="select select-bordered select-sm lg:select-md max-w-xs {rest.class || ''}"
>
	{#each options as option}
		<option value={option}>{option}</option>
	{/each}
</select>
