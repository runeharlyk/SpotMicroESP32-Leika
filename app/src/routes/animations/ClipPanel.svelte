<script lang="ts">
    import { ParamId, type Overlay, type ParamSpec } from '$lib/platform_shared/animation'
    import type { Editor, EditorState } from '$lib/animation/editor'
    import { Add, Delete } from '$lib/components/icons'

    const { editor, current }: { editor: Editor; current: EditorState } = $props()

    const LEGS = ['front left', 'front right', 'rear left', 'rear right']
    const BODY_CHANNELS = [
        'Body roll',
        'Body pitch',
        'Body yaw',
        'Body forward',
        'Body left',
        'Body up'
    ]
    const FOOT_AXES = ['forward', 'left', 'up']
    // An overlay channel as one select value: body axes first, then leg * 3 + axis of the feet.
    const CHANNELS = [
        ...BODY_CHANNELS.map((label, axis) => ({ value: `b${axis}`, label })),
        ...LEGS.flatMap((leg, l) =>
            FOOT_AXES.map((axis, a) => ({ value: `f${l * 3 + a}`, label: `Foot ${leg} ${axis}` }))
        )
    ]
    const PARAMS: [ParamId, string][] = [
        [ParamId.SPEED, 'Speed'],
        [ParamId.BODY_X, 'Body forward'],
        [ParamId.BODY_Y, 'Body sideways'],
        [ParamId.BODY_Z, 'Body height'],
        [ParamId.BODY_ROLL, 'Body roll'],
        [ParamId.BODY_PITCH, 'Body pitch'],
        [ParamId.BODY_YAW, 'Body yaw'],
        [ParamId.FOOT_LIFT, 'Foot lift'],
        [ParamId.OVERLAY_AMPLITUDE, 'Oscillation'],
        [ParamId.REPEAT, 'Repeats']
    ]

    const doc = $derived(current.document)
    const end = $derived(doc.keyframes[doc.keyframes.length - 1].time)
    const channelOf = (o: Overlay) =>
        o.bodyAxis !== undefined ? `b${o.bodyAxis}` : `f${o.footChannel}`

    const setOverlay = (index: number, change: Partial<Overlay>) =>
        editor.setOverlays(doc.overlays.map((o, i) => (i === index ? { ...o, ...change } : o)))

    const setChannel = (index: number, value: string) => {
        const n = Number(value.slice(1))
        setOverlay(
            index,
            value[0] === 'b' ?
                { bodyAxis: n, footChannel: undefined }
            :   { footChannel: n, bodyAxis: undefined }
        )
    }

    const addOverlay = () =>
        editor.setOverlays([
            ...doc.overlays,
            {
                bodyAxis: 0,
                footChannel: undefined,
                amplitude: 0.05,
                frequency: 1,
                phase: 0,
                start: 0,
                end
            }
        ])

    const setParam = (index: number, change: Partial<ParamSpec>) =>
        editor.setParams(doc.params.map((p, i) => (i === index ? { ...p, ...change } : p)))

    const unusedParam = $derived(PARAMS.find(([id]) => !doc.params.some(p => p.id === id)))
    const addParam = () => {
        if (!unusedParam) return
        const id = unusedParam[0]
        const spec =
            id === ParamId.SPEED ? { min: 0.5, defaultValue: 1, max: 2 }
            : id === ParamId.REPEAT ? { min: 1, defaultValue: 1, max: 3 }
            : { min: 0, defaultValue: 1, max: 1.5 }
        editor.setParams([...doc.params, { id, ...spec }])
    }

    // Clip values are float32: 1.3 is stored as 1.29999995, shown as typed.
    const shown = (v: number | undefined) => (v === undefined ? '' : Number(v.toPrecision(6)))
    const number = (e: Event) => Number((e.currentTarget as HTMLInputElement).value)
</script>

<div class="flex flex-col gap-3">
    <label class="flex flex-col text-sm">
        Name
        <input
            class="input input-sm"
            value={doc.name}
            onchange={e => editor.setClip({ name: e.currentTarget.value.trim() })}
        />
    </label>
    <label class="flex flex-col text-sm">
        Description
        <input
            class="input input-sm"
            value={doc.description}
            onchange={e => editor.setClip({ description: e.currentTarget.value })}
        />
    </label>
    <div class="flex flex-wrap gap-4 text-sm">
        <label class="flex items-center gap-2">
            <input
                type="checkbox"
                class="checkbox checkbox-sm"
                checked={doc.loop}
                onchange={e => editor.setClip({ loop: e.currentTarget.checked })}
            />
            Repeat until stopped
        </label>
        <label class="flex items-center gap-2">
            <input
                type="checkbox"
                class="checkbox checkbox-sm"
                checked={doc.holdEnd}
                onchange={e => editor.setClip({ holdEnd: e.currentTarget.checked })}
            />
            Hold the last pose until stopped
        </label>
    </div>
    <div class="grid grid-cols-3 gap-2">
        <label class="flex flex-col text-xs">
            Entry (s, 0 = 0.5)
            <input
                type="number"
                class="input input-sm"
                step="0.1"
                min="0"
                value={shown(doc.entryTime)}
                onchange={e => editor.setClip({ entryTime: number(e) })}
            />
        </label>
        <label class="flex flex-col text-xs">
            Exit (s, 0 = 0.5)
            <input
                type="number"
                class="input input-sm"
                step="0.1"
                min="0"
                value={shown(doc.exitTime)}
                onchange={e => editor.setClip({ exitTime: number(e) })}
            />
        </label>
        <label class="flex flex-col text-xs">
            Ride height (mm)
            <input
                type="number"
                class="input input-sm"
                placeholder="as it stands"
                value={shown(doc.rideHeight)}
                onchange={e =>
                    editor.setClip({
                        rideHeight: e.currentTarget.value === '' ? undefined : number(e)
                    })}
            />
        </label>
    </div>

    <h3 class="font-semibold">Oscillations</h3>
    {#each doc.overlays as overlay, i (i)}
        <div class="flex flex-wrap items-end gap-2" data-testid="overlay">
            <select
                class="select select-sm w-auto"
                aria-label="Channel"
                value={channelOf(overlay)}
                onchange={e => setChannel(i, e.currentTarget.value)}
            >
                {#each CHANNELS as channel (channel.value)}
                    <option value={channel.value}>{channel.label}</option>
                {/each}
            </select>
            {#each [['amplitude', 'Size'], ['frequency', 'Hz'], ['phase', 'Phase (rad)'], ['start', 'From (s)'], ['end', 'To (s)']] as [field, label] (field)}
                <label class="flex flex-col text-xs">
                    {label}
                    <input
                        type="number"
                        class="input input-sm w-20"
                        step="0.05"
                        value={shown(overlay[field as keyof Overlay] as number)}
                        onchange={e => setOverlay(i, { [field]: number(e) })}
                    />
                </label>
            {/each}
            <button
                class="btn btn-ghost btn-sm"
                aria-label="Remove oscillation"
                onclick={() => editor.setOverlays(doc.overlays.filter((_, j) => j !== i))}
            >
                <Delete class="h-4 w-4" />
            </button>
        </div>
    {/each}
    <div>
        <button class="btn btn-sm" disabled={doc.overlays.length >= 8} onclick={addOverlay}>
            <Add class="mr-1 h-4 w-4" />Oscillation
        </button>
    </div>

    <h3 class="font-semibold">Sliders offered when it plays</h3>
    {#each doc.params as param, i (param.id)}
        <div class="flex flex-wrap items-end gap-2" data-testid="param">
            <select
                class="select select-sm w-auto"
                aria-label="Slider"
                value={param.id}
                onchange={e => setParam(i, { id: Number(e.currentTarget.value) })}
            >
                {#each PARAMS as [id, label] (id)}
                    <option
                        value={id}
                        disabled={id !== param.id && doc.params.some(p => p.id === id)}
                    >
                        {label}
                    </option>
                {/each}
            </select>
            {#each [['min', 'Min'], ['defaultValue', 'Default'], ['max', 'Max']] as [field, label] (field)}
                <label class="flex flex-col text-xs">
                    {label}
                    <input
                        type="number"
                        class="input input-sm w-20"
                        step="0.1"
                        value={shown(param[field as keyof ParamSpec] as number)}
                        onchange={e => setParam(i, { [field]: number(e) })}
                    />
                </label>
            {/each}
            <label class="flex flex-col text-xs">
                Preview at {current.values[param.id]?.toFixed(2)}
                <input
                    type="range"
                    class="range range-xs w-28"
                    min={param.min}
                    max={param.max}
                    step={param.id === ParamId.REPEAT ? 1 : (param.max - param.min) / 100 || 0.01}
                    value={current.values[param.id] ?? param.defaultValue}
                    oninput={e => editor.setValue(param.id, number(e))}
                />
            </label>
            <button
                class="btn btn-ghost btn-sm"
                aria-label="Remove slider"
                onclick={() => editor.setParams(doc.params.filter((_, j) => j !== i))}
            >
                <Delete class="h-4 w-4" />
            </button>
        </div>
    {/each}
    <div>
        <button class="btn btn-sm" disabled={!unusedParam} onclick={addParam}>
            <Add class="mr-1 h-4 w-4" />Slider
        </button>
    </div>
</div>
