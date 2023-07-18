<script lang="ts">
	import Stream from '../components/Views/Stream.svelte';
	import Controls from '../components/Controls.svelte';
    import ModelView from '../components/Model/ModelView.svelte';
</script>

<div class="flex justify-center items-center w-full h-full">
	<Stream />
    <ModelView />
	<Controls />
</div>
