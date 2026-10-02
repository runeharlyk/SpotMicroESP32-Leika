import { describe, it, expect, vi, beforeEach } from 'vitest'
import { CorrelationResponse } from '../../src/lib/platform_shared/message'
import { APSettings, APStatus } from '../../src/lib/platform_shared/api'

const reply = vi.fn<() => Promise<CorrelationResponse>>()
vi.mock('$lib/stores/socket', () => ({ socket: { request: () => reply() } }))

const { robotRequest } = await import('../../src/lib/robot-request')

const response = (fields: Partial<CorrelationResponse>) =>
    CorrelationResponse.create({ correlationId: 1, ...fields })

describe('robotRequest', () => {
    beforeEach(() => reply.mockReset())

    it('resolves with the reply the robot accepted', async () => {
        const accepted = response({ statusCode: 200, apStatus: APStatus.create({ status: 1 }) })
        reply.mockResolvedValue(accepted)
        await expect(robotRequest({ apStatusRequest: {} })).resolves.toBe(accepted)
    })

    it("rejects with the robot's own reason when it refuses", async () => {
        reply.mockResolvedValue(response({ statusCode: 400, errorMessage: 'Invalid state' }))
        await expect(robotRequest({ apSettings: APSettings.create() })).rejects.toThrow(
            'Invalid state'
        )
    })

    it('names the status when the robot refuses without a reason', async () => {
        reply.mockResolvedValue(response({ statusCode: 400 }))
        await expect(robotRequest({ apSettings: APSettings.create() })).rejects.toThrow(
            'The robot replied with status 400'
        )
    })

    it('resolves a reply that says the work is still in progress', async () => {
        const scanning = response({ statusCode: 202 })
        reply.mockResolvedValue(scanning)
        await expect(robotRequest({ wifiNetworksRequest: {} })).resolves.toBe(scanning)
    })
})
