<script lang="ts">
    import { get } from 'svelte/store'
    import { modals } from 'svelte-modals'
    import RequiresRobot from '$lib/components/RequiresRobot.svelte'
    import ConfirmDialog from '$lib/components/ConfirmDialog.svelte'
    import { Cancel, Check } from '$lib/components/icons'
    import { reportedVariant } from '$lib/stores/featureFlags'
    import { createEditor } from '$lib/animation/editor'
    import { fetchClip, refreshClips, robotClips } from '$lib/animation/robot'
    import type { AnimationEntry } from '$lib/platform_shared/message'
    import Animations from './Animations.svelte'
    import Editor from './Editor.svelte'

    let view = $state<'library' | 'editor'>('library')
    let problem = $state<string | null>(null)
    const variant = $derived($reportedVariant ?? 'SPOTMICRO_ESP32')
    // A robot of another variant needs another preview and IK, so the editor starts over with it.
    const editor = $derived(createEditor(variant))

    /** Runs `replace` at once, or after asking when the open clip has edits that would be lost. */
    function discarding(replace: () => void) {
        if (!get(editor).dirty) return replace()
        modals.open(ConfirmDialog, {
            title: 'Discard changes',
            message: `${get(editor).document.name} has unsaved changes. Discard them?`,
            labels: {
                cancel: { label: 'Keep', icon: Cancel },
                confirm: { label: 'Discard', icon: Check }
            },
            onConfirm: () => {
                modals.close()
                replace()
            }
        })
    }

    const edit = (entry: AnimationEntry) =>
        discarding(async () => {
            problem = null
            try {
                editor.open(await fetchClip(entry))
                view = 'editor'
            } catch (error) {
                problem = (error as Error).message
            }
        })

    const startNew = () => discarding(() => editor.newDocument())
</script>

<div class="mx-0 my-1 flex flex-col space-y-4 sm:mx-8 sm:my-8">
    <RequiresRobot>
        <div role="tablist" class="tabs tabs-box self-start">
            <button
                role="tab"
                class="tab"
                class:tab-active={view === 'library'}
                onclick={() => (view = 'library')}>Library</button
            >
            <button
                role="tab"
                class="tab"
                class:tab-active={view === 'editor'}
                onclick={() => (view = 'editor')}>Editor</button
            >
        </div>
        {#if problem}
            <div class="alert alert-error" role="alert">{problem}</div>
        {/if}
        {#if view === 'library'}
            <Animations onEdit={edit} />
        {:else}
            <Editor
                {editor}
                {variant}
                clips={$robotClips}
                onUploaded={() => refreshClips().catch(() => {})}
                onNew={startNew}
            />
        {/if}
    </RequiresRobot>
</div>
