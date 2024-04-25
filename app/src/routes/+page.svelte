<script lang="ts">
	import type { PageData } from './$types';
	import { notifications } from '$lib/components/toasts/notifications';
	import Visualization from '$lib/components/Visualization.svelte';

	export let data: PageData;
</script>

<div class="hero bg-base-100 h-screen">
	<div class="card md:card-side bg-base-200 shadow-2xl flex justify-center items-center">
        <div class="w-64 h-64">
            <Visualization sky={false} orbit panel={false} ground={false}/>
        </div>
		<div class="card-body w-80">
			<h2 class="card-title text-center text-2xl">Welcome to {data.app_name}</h2>
			<p class="py-6 text-center"></p>
			<a
				class="btn btn-primary"
				href="/controller"
				on:click={() => notifications.success('You did it!', 1000)}>Begin</a
			>
		</div>
	</div>
</div>
