import { socket } from '$lib/stores/socket'
import { robotRequest } from '$lib/robot-request'

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

export type BootOutcome = 'running' | 'rolled back'

/**
 * Restarts the robot into the image just sent and tells, by the ELF hash it reports once back, whether that image
 * runs or the bootloader rolled back to the previous one.
 */
export async function bootNewFirmware(
    elfSha256: string,
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
            return reply.featuresDataResponse?.firmwareElfSha256 === elfSha256 ?
                    'running'
                :   'rolled back'
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
