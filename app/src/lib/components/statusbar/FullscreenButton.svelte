<script lang="ts">
    import { isFullscreen, toggleFullscreen } from '$lib/stores'
    import { MdiFullscreenExit, MdiFullscreen } from '../icons'

    const SvelteComponent = $derived($isFullscreen ? MdiFullscreenExit : MdiFullscreen)
</script>

<button
    onclick={toggleFullscreen}
    aria-label={$isFullscreen ? 'Exit fullscreen' : 'Enter fullscreen'}
>
    <SvelteComponent class="h-7 w-7" />
</button>
