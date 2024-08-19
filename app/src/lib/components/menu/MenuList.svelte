<script lang="ts">
	import { createEventDispatcher } from 'svelte';

	const dispatch = createEventDispatcher();

	type menuItem = {
		title: string;
		icon: ConstructorOfATypedSvelteComponent;
		href?: string;
		feature: boolean;
		active?: boolean;
		submenu?: menuItem[];
	};

	export let menuItems: menuItem[];

	export let level = 0;

	const selectMenuItem = (title: string) => {
		dispatch('select', title);
	};
</script>

<ul class={$$props.class + ' menu'}>
	{#each menuItems as menuItem, i (menuItem.title)}
		{#if menuItem.feature}
			<li>
				{#if menuItem.submenu}
					<details>
						<summary class="text-lg font-bold">
							<svelte:component this={menuItem.icon} class="h-6 w-6" />
							{menuItem.title}
						</summary>
						<div class="pl-4">
							<svelte:self menuItems={menuItem.submenu} level={level + 1} />
						</div>
					</details>
				{:else}
					<a
						href={menuItem.href}
						class="font-bold"
						class:bg-base-100={menuItem.active}
						class:text-lg={level === 0}
						class:text-md={level === 1}
						on:click={() => selectMenuItem(menuItem.title)}
					>
						<svelte:component this={menuItem.icon} class="h-6 w-6" />
						{menuItem.title}
					</a>
				{/if}
			</li>
		{/if}
	{/each}
</ul>
