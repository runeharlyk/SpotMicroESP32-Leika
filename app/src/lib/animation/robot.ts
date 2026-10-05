import { writable } from 'svelte/store'
import { socket } from '$lib/stores/socket'
import { robotRequest } from '$lib/robot-request'
import { fileSystemClient } from '$lib/filesystem/chunkedTransfer'
import { Animation } from '$lib/platform_shared/animation'
import { froundAnimation, parseAnimationJson, validate } from './model'
import type { AnimationEntry, AnimationParam, AnimationReport } from '$lib/platform_shared/message'

/** Where the firmware's AnimationStore looks for uploaded clips, relative to its file system. */
export const clipPath = (name: string) => `/animations/${name}.pb`

/** The robot's clips as it last listed them, shared by the library and the editor; null before it answered. */
export const robotClips = writable<AnimationEntry[] | null>(null)

export async function refreshClips(): Promise<AnimationEntry[]> {
    const clips = (await robotRequest({ animationListRequest: {} })).animationList?.animations ?? []
    robotClips.set(clips)
    return clips
}

/** The robot's answer for a clip: why it does not load, or what it offers and whether it stays in reach. */
export async function inspectClip(name: string): Promise<AnimationReport> {
    // A clip that does not load comes back as 422 with the report, so the report decides, not the status.
    const response = await socket.request({ animationValidate: { name } })
    if (!response.animationReport)
        throw new Error(`The robot replied with status ${response.statusCode}`)
    return response.animationReport
}

export async function playClip(name: string, params: AnimationParam[]): Promise<void> {
    await robotRequest({ animationPlay: { name, params } })
}

export async function stopClip(): Promise<void> {
    await robotRequest({ animationStop: {} })
}

/** Writes a validated clip to the robot and returns the robot's report; a clip the robot refuses is removed again. */
export async function uploadClip(clip: Animation): Promise<AnimationReport> {
    const path = clipPath(clip.name)
    const written = await fileSystemClient.uploadFile(path, Animation.encode(clip).finish())
    if (!written.success) throw new Error(written.error ?? 'The upload failed')
    const report = await inspectClip(clip.name)
    if (!report.ok) {
        await fileSystemClient.deleteFile(path)
        throw new Error(`The robot refused ${clip.name}: ${report.error}`)
    }
    return report
}

export async function deleteClip(name: string): Promise<void> {
    const deleted = await fileSystemClient.deleteFile(clipPath(name))
    if (!deleted.success) throw new Error(deleted.error ?? `Could not delete ${name}`)
}

// The firmware embeds the same files (esp32/scripts/pack_animations.py), so a built-in opens here as the robot plays it,
// unless the app and the firmware come from different versions.
const BUILTIN_JSON = import.meta.glob('../../../../animations/*.json', {
    eager: true,
    query: '?raw',
    import: 'default'
}) as Record<string, string>

/** A clip to edit: a built-in from the app's copy of animations/, an uploaded one read back from the robot. */
export async function fetchClip(entry: AnimationEntry): Promise<Animation> {
    if (entry.builtin) {
        const text = Object.entries(BUILTIN_JSON).find(([file]) =>
            file.endsWith(`/${entry.name}.json`)
        )?.[1]
        if (!text) throw new Error(`This app has no copy of the built-in ${entry.name}`)
        const parsed = parseAnimationJson(text)
        if ('error' in parsed) throw new Error(`${entry.name}: ${parsed.error}`)
        return parsed.animation
    }
    const read = await fileSystemClient.downloadFile(clipPath(entry.name))
    if (!read.success || !read.data) throw new Error(read.error ?? `Could not read ${entry.name}`)
    const clip = froundAnimation(Animation.decode(read.data))
    const error = validate(clip)
    if (error) throw new Error(`${entry.name} on the robot is broken: ${error}`)
    return clip
}
