<script lang="ts">
    import { legMode, type Editor, type EditorState, type LegMode } from '$lib/animation/editor'
    import { bodyOf, legTarget, type Vec3 } from '$lib/animation/player'

    const { editor, current }: { editor: Editor; current: EditorState } = $props()

    const LEG_NAMES = ['Front left', 'Front right', 'Rear left', 'Rear right']
    const DEG = 180 / Math.PI
    // Roll, pitch and yaw are stored in radians and shown in degrees.
    const BODY = [
        { label: 'Roll', unit: 'deg', scale: DEG },
        { label: 'Pitch', unit: 'deg', scale: DEG },
        { label: 'Yaw', unit: 'deg', scale: DEG },
        { label: 'Forward', unit: 'mm', scale: 1 },
        { label: 'Left', unit: 'mm', scale: 1 },
        { label: 'Up', unit: 'mm', scale: 1 }
    ]
    const MODES: [LegMode, string][] = [
        ['stance', 'Stands'],
        ['foot', 'Foot offset'],
        ['joints', 'Joint angles']
    ]
    const AXES: Record<LegMode, string[]> = {
        stance: ['Forward', 'Left', 'Up'],
        foot: ['Forward', 'Left', 'Up'],
        joints: ['Hip', 'Femur', 'Knee']
    }

    const keyframe = $derived(current.document.keyframes[current.selected])
    const body = $derived(bodyOf(keyframe))
    const shown = (v: number) => Number(v.toFixed(3))

    function setLegValue(leg: number, axis: number, value: number) {
        const v = [...legTarget(keyframe, leg).v] as Vec3
        v[axis] = value
        editor.setLeg(leg, v)
    }
</script>

<div class="flex flex-col gap-3">
    <h3 class="font-semibold">Body at {keyframe.time.toFixed(2)} s</h3>
    <div class="grid grid-cols-2 gap-2 sm:grid-cols-3">
        {#each BODY as axis, i (axis.label)}
            <label class="flex flex-col text-sm">
                {axis.label} ({axis.unit})
                <input
                    type="number"
                    class="input input-sm"
                    step={axis.unit === 'deg' ? 1 : 2}
                    value={shown(body[i] * axis.scale)}
                    onchange={e => editor.setBody(i, Number(e.currentTarget.value) / axis.scale)}
                />
            </label>
        {/each}
    </div>

    <h3 class="font-semibold">Legs</h3>
    {#each LEG_NAMES as name, leg (name)}
        {@const mode = legMode(keyframe, leg)}
        {@const values = legTarget(keyframe, leg).v}
        <div class="flex flex-col gap-1" data-testid={`leg-${leg}`}>
            <div class="flex items-center gap-2">
                <span class="w-24 text-sm">{name}</span>
                <select
                    class="select select-sm w-auto"
                    aria-label={`${name} target`}
                    value={mode}
                    onchange={e => editor.setLegMode(leg, e.currentTarget.value as LegMode)}
                >
                    {#each MODES as [value, label] (value)}
                        <option {value}>{label}</option>
                    {/each}
                </select>
            </div>
            <div class="grid grid-cols-3 gap-2">
                {#each AXES[mode] as label, axis (label)}
                    <label class="flex flex-col text-xs">
                        {label} ({mode === 'joints' ? 'deg' : 'mm'})
                        <input
                            type="number"
                            class="input input-sm"
                            step={mode === 'joints' ? 1 : 2}
                            value={shown(values[axis])}
                            onchange={e => setLegValue(leg, axis, Number(e.currentTarget.value))}
                        />
                    </label>
                {/each}
            </div>
        </div>
    {/each}
</div>
