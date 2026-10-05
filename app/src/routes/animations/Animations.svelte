<script lang="ts">
    import { onDestroy, onMount } from 'svelte'
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { Delete, Edit, Paw, Play, Reload, Stop, UploadIcon } from '$lib/components/icons'
    import { socket } from '$lib/stores/socket'
    import { mode } from '$lib/stores/model-store'
    import {
        AnimationState,
        AnimationStatus,
        ModeData,
        ModesEnum,
        type AnimationEntry,
        type AnimationReport
    } from '$lib/platform_shared/message'
    import { ParamId } from '$lib/platform_shared/animation'
    import { parseAnimationJson } from '$lib/animation/model'
    import {
        deleteClip,
        inspectClip,
        refreshClips,
        robotClips,
        playClip,
        stopClip,
        uploadClip
    } from '$lib/animation/robot'

    const { onEdit }: { onEdit: (clip: AnimationEntry) => void } = $props()

    const PARAM_LABELS: Record<ParamId, string> = {
        [ParamId.SPEED]: 'Speed',
        [ParamId.BODY_X]: 'Body forward',
        [ParamId.BODY_Y]: 'Body sideways',
        [ParamId.BODY_Z]: 'Body height',
        [ParamId.BODY_ROLL]: 'Body roll',
        [ParamId.BODY_PITCH]: 'Body pitch',
        [ParamId.BODY_YAW]: 'Body yaw',
        [ParamId.FOOT_LIFT]: 'Foot lift',
        [ParamId.OVERLAY_AMPLITUDE]: 'Oscillation',
        [ParamId.REPEAT]: 'Repeats',
        [ParamId.UNRECOGNIZED]: 'Unknown'
    }
    const STATE_LABELS: Record<AnimationState, string> = {
        [AnimationState.ANIM_IDLE]: 'Idle',
        [AnimationState.ANIM_ENTRY]: 'Moving into',
        [AnimationState.ANIM_PLAYING]: 'Playing',
        [AnimationState.ANIM_HOLD]: 'Holding',
        [AnimationState.ANIM_EXIT]: 'Returning from',
        [AnimationState.UNRECOGNIZED]: 'Unknown'
    }
    const LEG_NAMES = ['front left', 'front right', 'rear left', 'rear right']

    let selected = $state<string | null>(null)
    let report = $state<AnimationReport | null>(null)
    let values = $state<Record<number, number>>({})
    let status = $state<AnimationStatus | null>(null)
    let problem = $state<string | null>(null)
    let working = $state(false)
    let fileInput = $state<HTMLInputElement | undefined>()

    const deactivated = $derived($mode.mode === ModesEnum.DEACTIVATED)
    const running = $derived(!!status && status.state !== AnimationState.ANIM_IDLE)
    const selectedEntry = $derived($robotClips?.find(c => c.name === selected))
    const unreachableLegs = $derived(
        report ? LEG_NAMES.filter((_, leg) => (report!.clampedMask >> (leg * 3)) & 0b111) : []
    )

    async function attempt(action: () => Promise<void>) {
        working = true
        problem = null
        try {
            await action()
        } catch (error) {
            problem = (error as Error).message
        } finally {
            working = false
        }
    }

    const refresh = () =>
        attempt(async () => {
            const clips = await refreshClips()
            if (selected && !clips.some(c => c.name === selected)) select(null)
        })

    function select(name: string | null) {
        selected = name
        report = null
        values = {}
        if (!name) return
        attempt(async () => {
            const answer = await inspectClip(name)
            if (selected !== name) return
            report = answer
            values = Object.fromEntries(answer.params.map(p => [p.id, p.defaultValue]))
        })
    }

    const play = () =>
        attempt(() =>
            playClip(
                selected!,
                Object.entries(values).map(([id, value]) => ({ id: Number(id), value }))
            )
        )

    const stop = () => attempt(stopClip)

    const remove = (name: string) =>
        attempt(async () => {
            await deleteClip(name)
            await refreshClips()
            if (selected === name) select(null)
        })

    async function pickFile(event: Event) {
        const file = (event.currentTarget as HTMLInputElement).files?.[0]
        fileInput!.value = ''
        if (!file) return
        const parsed = parseAnimationJson(await file.text())
        if ('error' in parsed) {
            problem = `${file.name} is not a clip the robot plays: ${parsed.error}`
            return
        }
        const { animation } = parsed
        if ($robotClips?.some(c => c.builtin && c.name === animation.name)) {
            problem = `${animation.name} is built into the firmware; rename the clip to upload it`
            return
        }
        await attempt(async () => {
            await uploadClip(animation)
            await refreshClips()
        })
        if (!problem) select(animation.name)
    }

    const standUp = () => mode.set(ModeData.create({ mode: ModesEnum.STAND }))

    let stopStatus: (() => void) | undefined
    onMount(() => {
        stopStatus = socket.on(AnimationStatus, (data: AnimationStatus) => (status = data))
        refresh()
    })
    onDestroy(() => stopStatus?.())
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Paw class="mr-2 h-6 w-6 shrink-0 self-end" />
    {/snippet}
    {#snippet title()}
        <span>Animations</span>
    {/snippet}

    <div class="flex flex-col gap-4">
        <div class="flex flex-wrap items-center gap-2">
            <button class="btn btn-sm" disabled={working} onclick={refresh}>
                <Reload class="mr-1 h-4 w-4" />Refresh
            </button>
            <button class="btn btn-sm" disabled={working} onclick={() => fileInput!.click()}>
                <UploadIcon class="mr-1 h-4 w-4" />Upload a .json clip
            </button>
            <input
                type="file"
                accept=".json,application/json"
                class="hidden"
                bind:this={fileInput}
                onchange={pickFile}
            />
            <button class="btn btn-sm btn-error" disabled={!running} onclick={stop}>
                <Stop class="mr-1 h-4 w-4" />Stop
            </button>
        </div>

        <p aria-live="polite" data-testid="animation-status">
            {#if running}
                {STATE_LABELS[status!.state]} <strong>{status!.name}</strong>
                at {status!.t.toFixed(1)} s{status!.state === AnimationState.ANIM_HOLD ?
                    ', until stopped'
                :   ''}
            {:else}
                No clip is playing.
            {/if}
        </p>

        {#if problem}
            <div class="alert alert-error" role="alert">{problem}</div>
        {/if}

        {#if $robotClips === null}
            <p>Asking the robot for its clips...</p>
        {:else}
            <ul class="menu bg-base-200 rounded-box w-full">
                {#each $robotClips as clip (clip.name)}
                    <li>
                        <div
                            class="flex items-center gap-2"
                            class:menu-active={clip.name === selected}
                        >
                            <button class="flex-1 text-left" onclick={() => select(clip.name)}>
                                {clip.name}
                            </button>
                            <span class="badge badge-sm">
                                {clip.builtin ? 'built-in' : `${clip.size} B`}
                            </span>
                            <button
                                class="btn btn-ghost btn-xs"
                                aria-label={`Edit ${clip.name}`}
                                onclick={() => onEdit(clip)}
                            >
                                <Edit class="h-4 w-4" />
                            </button>
                            {#if !clip.builtin}
                                <button
                                    class="btn btn-ghost btn-xs"
                                    aria-label={`Delete ${clip.name}`}
                                    disabled={working}
                                    onclick={() => remove(clip.name)}
                                >
                                    <Delete class="h-4 w-4" />
                                </button>
                            {/if}
                        </div>
                    </li>
                {/each}
            </ul>
        {/if}

        {#if selectedEntry && report}
            <div class="flex flex-col gap-3" data-testid="clip-details">
                <h3 class="text-lg font-semibold">{selectedEntry.name}</h3>
                {#if report.description}<p>{report.description}</p>{/if}
                <p class="text-sm opacity-75">
                    {report.duration.toFixed(1)} s{report.loop ? ', repeats until stopped' : ''}{(
                        report.holdEnd
                    ) ?
                        ', holds its last pose until stopped'
                    :   ''}
                </p>
                {#if unreachableLegs.length}
                    <div class="alert alert-warning" role="alert">
                        The {unreachableLegs.join(', ')}
                        {unreachableLegs.length === 1 ? 'foot' : 'feet'} cannot reach every pose of this
                        clip on this robot; those legs stop at full stretch.
                    </div>
                {/if}
                {#each report.params as param (param.id)}
                    <label class="flex flex-col gap-1">
                        <span>
                            {PARAM_LABELS[param.id]}:
                            {param.id === ParamId.REPEAT ?
                                `${values[param.id]} times`
                            :   `${values[param.id].toFixed(2)} x`}
                        </span>
                        <input
                            type="range"
                            class="range range-sm"
                            min={param.min}
                            max={param.max}
                            step={param.id === ParamId.REPEAT ? 1 : (param.max - param.min) / 100}
                            bind:value={values[param.id]}
                        />
                    </label>
                {/each}
                {#if deactivated}
                    <div class="alert alert-warning" role="alert">
                        <span>The robot plays clips only once it stands.</span>
                        <button class="btn btn-sm" onclick={standUp}>Stand up</button>
                    </div>
                {/if}
                <div>
                    <button
                        class="btn btn-primary"
                        disabled={working || deactivated}
                        onclick={play}
                    >
                        <Play class="mr-1 h-5 w-5" />Play
                    </button>
                </div>
            </div>
        {/if}
    </div>
</SettingsCard>
