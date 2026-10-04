<script lang="ts">
    import { tick } from 'svelte'
    import { notifications } from '$lib/components/toasts/notifications'
    import { Copy, Delete } from '$lib/components/icons'
    import { serialLog, type SerialLogLine } from '$lib/stores/serial-log'

    let view: HTMLDivElement | undefined = $state()
    // Follows new lines until the reader scrolls up, and again once they scroll back to the end.
    let following = $state(true)

    const time = (at: number) => new Date(at).toLocaleTimeString([], { hour12: false })

    const asText = (lines: SerialLogLine[]) =>
        lines.map(line => `${time(line.at)} ${line.text}`).join('\n')

    $effect(() => {
        void $serialLog
        if (!following) return
        tick().then(() => view?.scrollTo({ top: view.scrollHeight }))
    })

    const onScroll = () => {
        if (!view) return
        following = view.scrollHeight - view.scrollTop - view.clientHeight < 8
    }

    const copyAll = async () => {
        try {
            await navigator.clipboard.writeText(asText($serialLog))
            notifications.success('Log copied', 2000)
        } catch (error) {
            notifications.error(`Copying the log failed: ${error}`, 4000)
        }
    }
</script>

<div class="bg-base-200 rounded-box flex flex-col p-3">
    <div class="mb-2 flex items-center gap-2">
        <span class="flex-1 font-semibold">Log</span>
        {#if !following}
            <button class="btn btn-ghost btn-xs" onclick={() => (following = true)}>
                Follow
            </button>
        {/if}
        <button class="btn btn-ghost btn-xs" onclick={copyAll} disabled={!$serialLog.length}>
            <Copy class="h-4 w-4" />
            Copy all
        </button>
        <button class="btn btn-ghost btn-xs" onclick={serialLog.clear}>
            <Delete class="h-4 w-4" />
            Clear
        </button>
    </div>
    <div
        bind:this={view}
        onscroll={onScroll}
        class="bg-base-300 rounded-box h-72 overflow-auto p-2 font-mono text-xs"
        role="log"
        aria-label="Robot log"
    >
        {#each $serialLog as line, index (index)}
            <div class="whitespace-pre-wrap break-all">
                <span class="opacity-50">{time(line.at)}</span>
                {line.text}
            </div>
        {:else}
            <div class="opacity-50">Nothing logged yet.</div>
        {/each}
    </div>
</div>
