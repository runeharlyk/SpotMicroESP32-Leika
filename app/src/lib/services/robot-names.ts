import { socket } from '$lib/stores/socket'
import { applyFeatures } from '$lib/stores/featureFlags'

/** Renames the connected robot on the robot itself; resolves to an error message, or null on success. */
export async function renameConnectedRobot(name: string): Promise<string | null> {
    try {
        const response = await socket.request({ robotNameUpdate: { name: name.trim() } })
        if (response.featuresDataResponse) applyFeatures(response.featuresDataResponse)
        if (response.statusCode !== 200)
            return 'The robot rejected the name; use 1 to 32 characters'
        return null
    } catch (error) {
        return error instanceof Error ? error.message : String(error)
    }
}
