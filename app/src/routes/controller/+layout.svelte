<script lang="ts">
	import Controls from './Controls.svelte';
	import WidgetContainer from '$lib/components/layout/WidgetContainer.svelte';
	import { selectedView, views } from '$lib/stores/application';

    $: layout = $views.find(v => v.name === $selectedView)!
</script>

<div class="absolute top-0 select-none w-screen h-screen">
	<Controls />
	<div class="absolute w-full h-screen top-0 overflow-hidden lg:pt-16 pt-12">
		<WidgetContainer container={layout.content} />
	</div>
</div>
