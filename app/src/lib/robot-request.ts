import { socket } from '$lib/stores/socket'
import type { CorrelationRequest, CorrelationResponse } from '$lib/platform_shared/message'

/**
 * A request to the robot over the socket that fails the way a page can show: with the robot's own
 * reason when it refuses. Replies below 400, including 202 (still working), resolve.
 */
export async function robotRequest(
    data: Omit<CorrelationRequest, 'correlationId'>
): Promise<CorrelationResponse> {
    const response = await socket.request(data)
    if (response.statusCode >= 400)
        throw new Error(
            response.errorMessage || `The robot replied with status ${response.statusCode}`
        )
    return response
}
