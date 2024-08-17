<script lang="ts">
	import Controls from './Controls.svelte';
	import WidgetContainer from '$lib/components/layout/DynamicLayout.svelte';
	import { controllerLayout, phoneControllerLayout} from '$lib/components/layout/LayoutManager';
</script>

<div class="absolute top-0 select-none w-screen h-screen">
	<Controls />
	<!-- <button class="absolute z-20 btn" on:click={addWidget}>Add Widget</button> -->
	<div class="absolute w-full h-screen top-0 overflow-hidden lg:pt-16 pt-12">
		<WidgetContainer container={$phoneControllerLayout} />
	</div>
</div>
