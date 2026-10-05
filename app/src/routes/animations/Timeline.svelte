<script lang="ts">
    import { Ease } from '$lib/platform_shared/animation'
    import type { Editor, EditorState } from '$lib/animation/editor'
    import { Add, Delete, Play, Stop } from '$lib/components/icons'

    const { editor, current }: { editor: Editor; current: EditorState } = $props()

    const EASES: [Ease, string][] = [
        [Ease.LINEAR, 'Linear'],
        [Ease.EASE_IN, 'Ease in'],
        [Ease.EASE_OUT, 'Ease out'],
        [Ease.EASE_IN_OUT, 'Ease in and out']
    ]

    let track: HTMLDivElement
    let dragged = $state<number | null>(null)
    let dragTime = $state(0)

    const keyframes = $derived(current.document.keyframes)
    const span = $derived(Math.max(1, keyframes[keyframes.length - 1].time))
    const selected = $derived(keyframes[current.selected])
    const percent = (t: number) => `${(Math.min(t, span) / span) * 100}%`

    const timeAt = (event: PointerEvent) => {
        const rect = track.getBoundingClientRect()
        return Math.max(0, ((event.clientX - rect.left) / rect.width) * span)
    }

    function grab(event: PointerEvent, index: number) {
        event.stopPropagation()
        editor.select(index)
        if (index === 0) return
        dragged = index
        dragTime = keyframes[index].time
        track.setPointerCapture(event.pointerId)
    }

    function move(event: PointerEvent) {
        if (dragged !== null) dragTime = Math.round(timeAt(event) * 20) / 20
    }

    function release(event: PointerEvent) {
        if (dragged === null) return editor.scrubTo(timeAt(event))
        if (dragTime !== keyframes[dragged].time) editor.setTime(dragged, dragTime)
        dragged = null
    }
</script>

<div class="flex flex-col gap-2">
    <div class="flex flex-wrap items-center gap-2">
        {#if current.playing}
            <button class="btn btn-sm" onclick={() => editor.pause()}>
                <Stop class="mr-1 h-4 w-4" />Stop preview
            </button>
        {:else}
            <button class="btn btn-sm" onclick={() => editor.play()}>
                <Play class="mr-1 h-4 w-4" />Preview
            </button>
        {/if}
        <button class="btn btn-sm" onclick={() => editor.addKeyframe()}>
            <Add class="mr-1 h-4 w-4" />Keyframe
        </button>
        <button
            class="btn btn-sm"
            aria-label="Delete keyframe"
            disabled={current.selected === 0}
            onclick={() => editor.deleteKeyframe(current.selected)}
        >
            <Delete class="h-4 w-4" />
        </button>
        <label class="flex items-center gap-1 text-sm">
            At
            <input
                type="number"
                class="input input-sm w-20"
                step="0.05"
                min="0"
                aria-label="Keyframe time"
                disabled={current.selected === 0}
                value={selected.time}
                onchange={e => editor.setTime(current.selected, Number(e.currentTarget.value))}
            />
            s
        </label>
        <select
            class="select select-sm w-auto"
            aria-label="Ease into this keyframe"
            value={selected.ease}
            onchange={e => editor.setEase(Number(e.currentTarget.value))}
        >
            {#each EASES as [ease, label] (ease)}
                <option value={ease}>{label}</option>
            {/each}
        </select>
        <span class="text-sm opacity-75">{current.scrub.toFixed(2)} s</span>
    </div>

    <div
        bind:this={track}
        class="bg-base-200 rounded-box relative h-10 w-full cursor-pointer touch-none"
        role="slider"
        tabindex="0"
        aria-label="Clip time"
        aria-valuemin={0}
        aria-valuemax={span}
        aria-valuenow={current.scrub}
        onpointermove={move}
        onpointerup={release}
    >
        <div
            class="bg-primary absolute top-0 h-full w-0.5"
            style:left={percent(current.scrub)}
        ></div>
        {#each keyframes as keyframe, index (keyframe)}
            <button
                class="absolute top-1/2 h-4 w-4 -translate-x-1/2 -translate-y-1/2 rotate-45 border"
                class:bg-secondary={index === current.selected}
                class:bg-base-content={index !== current.selected}
                style:left={percent(dragged === index ? dragTime : keyframe.time)}
                aria-label={`Keyframe at ${keyframe.time.toFixed(2)} s`}
                onpointerdown={e => grab(e, index)}
            ></button>
        {/each}
    </div>
</div>
