<script lang="ts">
    import { socket } from '$lib/stores/socket'

    const transport = socket.transport
    const labels = { websocket: 'WiFi', bluetooth: 'BLE' }
    const label = $derived($socket ? (labels[$transport ?? 'websocket'] ?? 'Online') : 'Offline')
</script>

<div
    class="tooltip tooltip-left"
    data-tip={$socket ? `Connected over ${label}` : 'Reconnecting to robot'}
>
    <span class="badge badge-sm badge-ghost gap-1.5 font-medium" role="status" aria-live="polite">
        <span class="size-1.5 rounded-full {$socket ? 'bg-success' : 'bg-error animate-pulse'}"
        ></span>
        <span class="hidden sm:inline">{label}</span>
    </span>
</div>
