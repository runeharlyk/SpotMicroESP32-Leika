import { socket } from '$lib/stores/socket'
import { applyFeatures } from '$lib/stores/featureFlags'

// The robot holds a name of up to 32 bytes; a longer one cannot even be decoded, so it would never be answered.
const ROBOT_NAME_MAX_BYTES = 32
const NAME_RULE = 'use 1 to 32 characters, fewer with accents or symbols'

/** Renames the connected robot on the robot itself; resolves to an error message, or null on success. */
export async function renameConnectedRobot(name: string): Promise<string | null> {
    const trimmed = name.trim()
    if (new TextEncoder().encode(trimmed).length > ROBOT_NAME_MAX_BYTES)
        return `The name is too long; ${NAME_RULE}`
    try {
        const response = await socket.request({ robotNameUpdate: { name: trimmed } })
        if (response.featuresDataResponse) applyFeatures(response.featuresDataResponse)
        if (response.statusCode !== 200) return `The robot rejected the name; ${NAME_RULE}`
        return null
    } catch (error) {
        return error instanceof Error ? error.message : String(error)
    }
}
