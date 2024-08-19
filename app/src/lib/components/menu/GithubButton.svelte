<script lang="ts">
	import { Github } from "../icons";

	export let github;
</script>

{#if github.active}
	<a href={github.href} class="btn btn-ghost" target="_blank" rel="noopener noreferrer">
        <Github class="h-5 w-5" />
    </a>
{/if}
