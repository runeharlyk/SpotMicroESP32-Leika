<script lang="ts">
    import { socket } from '$lib/stores/socket'
</script>

<div
    class="tooltip tooltip-left"
    data-tip={$socket ? 'Connected to robot' : 'Reconnecting to robot'}
>
    <span class="badge badge-sm badge-ghost gap-1.5 font-medium" role="status" aria-live="polite">
        <span class="size-1.5 rounded-full {$socket ? 'bg-success' : 'bg-error animate-pulse'}"
        ></span>
        <span class="hidden sm:inline">{$socket ? 'Online' : 'Offline'}</span>
    </span>
</div>
