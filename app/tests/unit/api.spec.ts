import { describe, it, expect, vi, afterEach } from 'vitest'
import { api } from '../../src/lib/api'

describe('api error messages', () => {
    afterEach(() => vi.unstubAllGlobals())

    it('names the HTTP status of a failed response rather than blaming authorization', async () => {
        vi.stubGlobal(
            'fetch',
            vi.fn(async () => new Response(null, { status: 500 }))
        )

        const result = await api.get('/api/ap/settings')

        expect(result.isErr()).toBe(true)
        expect(result.isErr() && result.inner.message).toMatch(/HTTP 500/)
        expect(result.isErr() && result.inner.message).not.toMatch(/authori/i)
    })

    it('reports an unreachable robot as a network failure', async () => {
        vi.stubGlobal(
            'fetch',
            vi.fn(async () => {
                throw new TypeError('Failed to fetch')
            })
        )

        const result = await api.get('/api/ap/settings')

        expect(result.isErr() && result.inner.message).toMatch(/could not reach the robot/i)
    })

    it('reports a 401 as an authorization failure', async () => {
        vi.stubGlobal(
            'fetch',
            vi.fn(async () => new Response(null, { status: 401 }))
        )

        const result = await api.get('/api/ap/settings')

        expect(result.isErr() && result.inner.message).toMatch(/not authorized/i)
    })
})
