<script lang="ts">
    import { isFullscreen, toggleFullscreen } from '$lib/stores';
	import { MdiFullscreenExit, MdiFullscreen } from '../icons';
</script>

<button on:click={toggleFullscreen}>
    <svelte:component this={$isFullscreen ? MdiFullscreenExit : MdiFullscreen} class="h-7 w-7" />
</button>