import { vi } from 'vitest'
import { socket } from '../../src/lib/stores/socket'
import { CorrelationResponse } from '../../src/lib/platform_shared/message'

type RequestData = Parameters<typeof socket.request>[0]
type Reply = Partial<CorrelationResponse>

/**
 * Stands in for the robot at the socket: every request is answered by `answer`, keyed by the
 * request's field name (e.g. 'apStatusRequest'), and each sent request's name is recorded.
 * Replies default to status 200, as the firmware sends them.
 */
export function fakeRobot(answer: (name: string, data: RequestData) => Reply | Promise<Reply>) {
    const sent: string[] = []
    const spy = vi.spyOn(socket, 'request').mockImplementation(async data => {
        const name = Object.entries(data).find(([, value]) => value !== undefined)![0]
        sent.push(name)
        return CorrelationResponse.create({
            correlationId: 1,
            statusCode: 200,
            ...(await answer(name, data))
        })
    })
    return { sent, restore: () => spy.mockRestore() }
}
