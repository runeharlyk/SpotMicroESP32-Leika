<script lang="ts">
    import AnimationPreview from '$lib/components/AnimationPreview.svelte'
    import { DownloadIcon, Play, UploadIcon } from '$lib/components/icons'
    import { legMode, type Editor } from '$lib/animation/editor'
    import { legTarget } from '$lib/animation/player'
    import { playClip, uploadClip } from '$lib/animation/robot'
    import type { AnimationEntry } from '$lib/platform_shared/message'
    import type { Variant } from '$lib/simulation/firmware/kin-config'
    import { saveFile } from '$lib/utilities/save-file'
    import Timeline from './Timeline.svelte'
    import PosePanel from './PosePanel.svelte'
    import ClipPanel from './ClipPanel.svelte'

    interface Props {
        editor: Editor
        variant: Variant
        /** The robot's clips, to refuse saving over a built-in; null when the robot has not answered. */
        clips: AnimationEntry[] | null
        onUploaded: () => void
        onNew: () => void
    }

    const { editor, variant, clips, onUploaded, onNew }: Props = $props()

    let working = $state(false)
    let problem = $state<string | null>(null)

    const current = $derived($editor)
    const keyframe = $derived(current.document.keyframes[current.selected])
    // Joint legs are edited in the pose panel; a handle moves a foot.
    const handles = $derived(
        current.playing ?
            []
        :   [0, 1, 2, 3]
                .filter(leg => legMode(keyframe, leg) !== 'joints')
                .map(leg => ({ leg, offset: legTarget(keyframe, leg).v }))
    )
    const builtinName = $derived(
        clips?.some(c => c.builtin && c.name === current.document.name) ?? false
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

    const save = async () => {
        if (builtinName)
            throw new Error(
                `${current.document.name} is built into the firmware; rename the clip to upload it`
            )
        await uploadClip(current.document)
        editor.markSaved()
        onUploaded()
    }

    const download = () =>
        saveFile(editor.toJson(), `${current.document.name}.json`, 'application/json')

    const playOnRobot = () =>
        attempt(async () => {
            if (current.dirty || !clips?.some(c => c.name === current.document.name)) await save()
            await playClip(
                current.document.name,
                Object.entries(current.values).map(([id, value]) => ({ id: Number(id), value }))
            )
        })
</script>

<div class="flex flex-col gap-4">
    <div class="flex flex-wrap items-center gap-2">
        <button class="btn btn-sm" onclick={onNew}>New clip</button>
        <button class="btn btn-sm" disabled={!!current.error} onclick={download}>
            <DownloadIcon class="mr-1 h-4 w-4" />Download JSON
        </button>
        <button
            class="btn btn-sm"
            disabled={working || !!current.error}
            onclick={() => attempt(save)}
        >
            <UploadIcon class="mr-1 h-4 w-4" />Save to robot
        </button>
        <button
            class="btn btn-sm btn-primary"
            disabled={working || !!current.error}
            onclick={playOnRobot}
        >
            <Play class="mr-1 h-4 w-4" />Play on robot
        </button>
        <span class="text-sm opacity-75" data-testid="document-state">
            {current.document.name}{current.dirty ? ', unsaved' : ''}
        </span>
    </div>

    {#if current.error}
        <div class="alert alert-warning" role="alert">{current.error}</div>
    {/if}
    {#if problem}
        <div class="alert alert-error" role="alert">{problem}</div>
    {/if}
    {#if current.frame.mask}
        <div class="alert alert-warning" role="alert">
            A foot is out of reach here; that leg stops at full stretch.
        </div>
    {/if}

    <div class="grid gap-4 lg:grid-cols-[3fr_2fr]">
        <div class="flex flex-col gap-3">
            <div class="h-80 lg:h-[28rem]">
                <AnimationPreview
                    {variant}
                    frame={current.frame}
                    {handles}
                    onMove={(leg, offset) => editor.setLeg(leg, offset)}
                    onTick={dt => editor.tick(dt)}
                />
            </div>
            <Timeline {editor} {current} />
        </div>
        <div class="flex flex-col gap-6">
            <PosePanel {editor} {current} />
            <ClipPanel {editor} {current} />
        </div>
    </div>
</div>
