import { socket } from '$lib/stores/socket'
import { robotRequest } from '$lib/robot-request'
import { fileSystemClient } from '$lib/filesystem/chunkedTransfer'
import { Animation } from '$lib/platform_shared/animation'
import type { AnimationEntry, AnimationParam, AnimationReport } from '$lib/platform_shared/message'

/** Where the firmware's AnimationStore looks for uploaded clips, relative to its file system. */
export const clipPath = (name: string) => `/animations/${name}.pb`

export async function listClips(): Promise<AnimationEntry[]> {
    return (await robotRequest({ animationListRequest: {} })).animationList?.animations ?? []
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
