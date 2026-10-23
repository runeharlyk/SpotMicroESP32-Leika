import { describe, it, expect, afterEach } from 'vitest'
import { get } from 'svelte/store'
import { WebSocketServer, type WebSocket } from 'ws'
import { socket } from '../../src/lib/stores/socket'
import { connectionFeatures, useFeatureFlags } from '../../src/lib/stores/featureFlags'
import {
    CorrelationResponse,
    FeaturesDataResponse,
    Message
} from '../../src/lib/platform_shared/message'

// What the app knows of a robot follows the robot the user chose, not the link to it.
describe.sequential('the feature flags', () => {
    const servers: WebSocketServer[] = []
    let port = 9400

    // A robot that answers the features request with `features` once `answering` is set.
    async function robot(features: Partial<FeaturesDataResponse>) {
        port++
        const server = new WebSocketServer({ port })
        servers.push(server)
        await new Promise(resolve => server.on('listening', resolve))
        const state = { asked: 0, answering: true, drop: () => {} }
        server.on('connection', (client: WebSocket) => {
            state.drop = () => client.terminate()
            client.on('message', (frame: Buffer) => {
                const request = Message.decode(new Uint8Array(frame)).correlationRequest
                if (!request?.featuresDataRequest) return
                state.asked++
                if (!state.answering) return
                const response = CorrelationResponse.create({
                    correlationId: request.correlationId,
                    statusCode: 200,
                    featuresDataResponse: FeaturesDataResponse.create(features)
                })
                client.send(
                    Message.encode(Message.create({ correlationResponse: response })).finish()
                )
            })
        })
        return { url: `ws://localhost:${port}`, state }
    }

    const flags = () => get(useFeatureFlags())

    afterEach(async () => {
        for (const server of servers.splice(0)) {
            server.clients.forEach(client => client.terminate())
            await new Promise(resolve => server.close(resolve))
        }
    })

    it('keeps them through a drop and reads them again when the robot is back', async () => {
        const leika = await robot({ robotName: 'leika', imu: true })
        socket.init(leika.url)
        await expect.poll(() => get(connectionFeatures)?.robotName).toBe('leika')

        leika.state.drop()
        await expect.poll(() => get(socket)).toBe(false)

        expect(get(connectionFeatures)?.robotName).toBe('leika')
        expect(flags().imu).toBe(true)
        await expect.poll(() => leika.state.asked, { timeout: 3000 }).toBe(2)
        expect(get(connectionFeatures)?.robotName).toBe('leika')
    })

    it('forgets them when the user connects another robot, before it answers', async () => {
        const leika = await robot({ robotName: 'leika', imu: true })
        socket.init(leika.url)
        await expect.poll(() => get(connectionFeatures)?.robotName).toBe('leika')
        const pico = await robot({ robotName: 'pico' })
        pico.state.answering = false

        socket.init(pico.url)
        await expect.poll(() => pico.state.asked).toBe(1)

        expect(get(connectionFeatures)).toBeNull()
        expect(flags().imu).toBeUndefined()
    })
})
