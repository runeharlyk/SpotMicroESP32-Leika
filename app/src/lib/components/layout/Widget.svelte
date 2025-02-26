<script lang="ts">
    interface Props {
        children?: import('svelte').Snippet;
    }

    let { children }: Props = $props();
</script>

<div class="box-border overflow-hidden flex-1">
    {@render children?.()}
</div>
