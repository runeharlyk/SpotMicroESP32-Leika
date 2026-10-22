import { socket } from '$lib/stores/socket'
import type { CorrelationRequest, CorrelationResponse } from '$lib/platform_shared/message'

// The firmware answers a request it does not know with 501; firmware from before answered it with a 400 and this
// reason, which it gives no other refusal.
const UNKNOWN_REQUEST = 501
const OLD_UNKNOWN_REQUEST = 'Unknown request'
export const UNHANDLED =
    "The robot's firmware does not handle this: it is older, or built without it"

const unhandled = (response: CorrelationResponse) =>
    response.statusCode === UNKNOWN_REQUEST ||
    (response.statusCode === 400 && response.errorMessage === OLD_UNKNOWN_REQUEST)

/**
 * A request to the robot over the socket that fails the way a page can show: with the robot's own
 * reason when it refuses, and as a build that does not handle the request when it does not know it:
 * one older than the request, or one built without it, as a board without a camera is. Replies
 * below 400, including 202 (still working), resolve.
 */
export async function robotRequest(
    data: Omit<CorrelationRequest, 'correlationId'>
): Promise<CorrelationResponse> {
    const response = await socket.request(data)
    if (unhandled(response)) throw new Error(UNHANDLED)
    if (response.statusCode >= 400)
        throw new Error(
            response.errorMessage || `The robot replied with status ${response.statusCode}`
        )
    return response
}
