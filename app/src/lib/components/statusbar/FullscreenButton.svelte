<script lang="ts">
    import { isFullscreen, toggleFullscreen } from '$lib/stores';
    import MdiFullscreen from '~icons/mdi/fullscreen';
	import MdiFullscreenExit from '~icons/mdi/fullscreen-exit';
</script>

<button on:click={toggleFullscreen}>
    <svelte:component this={$isFullscreen ? MdiFullscreenExit : MdiFullscreen} class="h-7 w-7" />
</button>