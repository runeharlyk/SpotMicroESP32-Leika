<script lang="ts">
	import MdiGithub from '~icons/mdi/github';
	export let github;
</script>

{#if github.active}
	<a href={github.href} class="btn btn-ghost" target="_blank" rel="noopener noreferrer"
		><MdiGithub class="h-5 w-5" />
    </a>
{/if}
