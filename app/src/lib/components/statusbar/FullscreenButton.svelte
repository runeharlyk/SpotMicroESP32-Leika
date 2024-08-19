<script lang="ts">
    import { isFullscreen, toggleFullscreen } from '$lib/stores';
    import MdiFullscreen from '~icons/mdi/fullscreen';
	import MdiFullscreenExit from '~icons/mdi/fullscreen-exit';
</script>

<!-- svelte-ignore a11y-click-events-have-key-events -->
<!-- svelte-ignore a11y-no-static-element-interactions -->
<div on:click={toggleFullscreen}>
    <svelte:component this={isFullscreen ? MdiFullscreenExit : MdiFullscreen} class="h-7 w-7" />
</div>