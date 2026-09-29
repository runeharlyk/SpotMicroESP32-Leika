<script lang="ts">
    import { Reload, Warning } from './icons'

    interface Props {
        error: unknown
        retry: () => void
    }

    const { error, retry }: Props = $props()

    const message = $derived(error instanceof Error ? error.message : String(error))
</script>

<div role="alert" class="alert alert-error alert-soft">
    <Warning class="h-6 w-6 shrink-0" />
    <span>{message}</span>
    <button class="btn btn-sm" onclick={retry}>
        <Reload class="h-4 w-4" />
        Retry
    </button>
</div>
