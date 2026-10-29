import { socket } from '$lib/stores/socket'
import { robotRequest } from '$lib/robot-request'
import { OtaDownloadState, OtaStatus } from '$lib/platform_shared/message'
import type { Follower } from '$lib/robot/watch'

/** What the robot's OtaChunk holds (platform_shared/message.options). */
export const OTA_CHUNK_SIZE = 2 ** 14
// Enough chunks on their way to hide the round trip, few enough that a refusal stops the rest soon.
const CHUNKS_IN_FLIGHT = 4

/**
 * Writes an app image into the robot's free update slot, in order, and has the robot validate it and boot it next.
 * Fails with the robot's reason on the first refusal; the robot then keeps booting the firmware it runs.
 */
export async function sendFirmware(image: Uint8Array, onProgress: (fraction: number) => void) {
    await robotRequest({ otaStart: { size: image.length } })
    const chunks = Math.ceil(image.length / OTA_CHUNK_SIZE)
    let next = 0
    let sent = 0
    let refused = false
    const sendChunks = async () => {
        while (next < chunks && !refused) {
            const index = next++
            const data = image.subarray(index * OTA_CHUNK_SIZE, (index + 1) * OTA_CHUNK_SIZE)
            try {
                await robotRequest({ otaChunk: { index, data } })
            } catch (error) {
                refused = true
                throw error
            }
            sent += data.length
            onProgress(sent / image.length)
        }
    }
    await Promise.all(Array.from({ length: CHUNKS_IN_FLIGHT }, sendChunks))
    await robotRequest({ otaFinish: {} })
}

/**
 * Has the robot download the app image at `url` itself, over its own WiFi, into its free update slot, as sendFirmware
 * writes it; resolves once the robot validated and selected it, and fails with the robot's reason, or when it stops
 * reporting for `silentForMs` (it reports every half second while it downloads).
 *
 * The robot keeps the last download's end as its update status, and answers the subscribe with it, so the download is
 * asked for only once that answer is in, live or failed, and only the statuses after it are this download's. Firmware
 * from before the state topics answers nothing and pushes only changes.
 */
export async function downloadFirmwareOnRobot(
    url: string,
    onProgress: (fraction: number) => void,
    { silentForMs = 30000, from = socket as Follower } = {}
): Promise<void> {
    let stopFollowing = () => {}
    let silence: ReturnType<typeof setTimeout> | undefined
    let asked = false
    let seen: OtaStatus | undefined
    let answered = () => {}
    const subscribed = new Promise<void>(resolve => (answered = resolve))
    const finished = new Promise<void>((resolve, reject) => {
        const quiet = () => {
            clearTimeout(silence)
            silence = setTimeout(
                () => reject(new Error('The robot stopped reporting the download')),
                silentForMs
            )
        }
        stopFollowing = from.follow(OtaStatus, ({ value, status }) => {
            if (!asked) {
                seen = value
                if (status !== 'subscribing') answered()
                return
            }
            if (value === undefined || value === seen) return
            seen = value
            quiet()
            if (value.size > 0) onProgress(value.written / value.size)
            if (value.state === OtaDownloadState.OTA_DOWNLOADED) resolve()
            if (value.state === OtaDownloadState.OTA_DOWNLOAD_FAILED)
                reject(new Error(value.error || 'The download failed'))
        })
        quiet()
    })
    try {
        await Promise.race([subscribed, finished])
        asked = true
        await robotRequest({ otaDownload: { url } })
        await finished
    } finally {
        clearTimeout(silence)
        stopFollowing()
    }
}

export type BootOutcome = 'running' | 'rolled back'

/**
 * Restarts the robot into the image just written and tells, by the ELF hash it reports once back, whether that image
 * runs or the bootloader rolled back to the previous one.
 */
export async function bootNewFirmware(
    isNewImage: (elfSha256: string | undefined) => boolean,
    { backWithinMs = 60000, retryAfterMs = 1000 } = {}
): Promise<BootOutcome> {
    const deadline = Date.now() + backWithinMs
    let stopListening = () => {}
    const closed = new Promise<void>(
        resolve => (stopListening = socket.onEvent('close', () => resolve()))
    )
    try {
        await robotRequest({ systemRestart: {} })
        // An answer before the socket dropped would still come from the old firmware.
        await Promise.race([closed, delay(backWithinMs)])
    } finally {
        stopListening()
    }
    for (;;) {
        try {
            const reply = await robotRequest({ featuresDataRequest: {} })
            return isNewImage(reply.featuresDataResponse?.firmwareElfSha256) ? 'running' : (
                    'rolled back'
                )
        } catch {
            if (Date.now() >= deadline)
                throw new Error(
                    'The robot did not come back after the restart; connect it over USB to see why'
                )
            await delay(retryAfterMs)
        }
    }
}

const delay = (ms: number) => new Promise(resolve => setTimeout(resolve, ms))
