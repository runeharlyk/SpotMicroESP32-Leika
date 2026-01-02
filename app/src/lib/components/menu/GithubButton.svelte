<script lang="ts">
    import { Github } from '../icons'

    interface Props {
        github: { href: string; active?: boolean }
    }

    let { github }: Props = $props()
</script>

{#if github.active}
    <!-- eslint-disable-next-line svelte/no-navigation-without-resolve -- external URL -->
    <a href={github.href} class="btn btn-ghost" target="_blank" rel="noopener noreferrer">
        <Github class="h-5 w-5" />
    </a>
{/if}
