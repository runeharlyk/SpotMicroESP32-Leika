import { describe, it, expect, afterEach } from 'vitest'
import { WebSocketServer, type WebSocket } from 'ws'
import { createWebSocket } from '../../src/lib/stores/socket'
import { Message } from '../../src/lib/platform_shared/message'

// Requests against a real WebSocket server playing a robot that never answers them.
describe.sequential('socket requests', () => {
    let wss: WebSocketServer | undefined
    let port = 9300

    async function silentRobot(onClient: (client: WebSocket) => void = () => {}) {
        port++
        wss = new WebSocketServer({ port })
        await new Promise(resolve => wss!.on('listening', resolve))
        wss.on('connection', onClient)
        return `ws://localhost:${port}`
    }

    afterEach(async () => {
        wss?.clients.forEach(client => client.terminate())
        await new Promise(resolve => wss?.close(resolve))
        wss = undefined
    })

    // Telemetry flows at 20 Hz: before, every frame re-armed every pending timeout, so none ever fired.
    it('times out a request that is never answered while telemetry keeps arriving', async () => {
        const url = await silentRobot(client => {
            const telemetry = setInterval(() => {
                client.send(Message.encode(Message.create({ imu: { x: 1 } })).finish())
            }, 50)
            client.on('close', () => clearInterval(telemetry))
        })
        const socket = createWebSocket({ requestTimeoutTime: 300 })
        socket.init(url)
        const started = Date.now()

        await expect(socket.request({ featuresDataRequest: {} })).rejects.toThrow(/timeout/i)
        expect(Date.now() - started).toBeLessThan(1500)
    })

    // A request sent on a link that died can never be answered; its caller learns so at once.
    it('rejects the pending requests when the connection drops', async () => {
        let robotSide: WebSocket | undefined
        const received = new Promise<void>(resolve => {
            void silentRobot(client => {
                robotSide = client
                client.on('message', () => resolve())
            }).then(url => socket.init(url))
        })
        const socket = createWebSocket({ requestTimeoutTime: 10000 })
        const pending = socket.request({ featuresDataRequest: {} })
        await received
        const dropped = Date.now()
        robotSide!.terminate()

        await expect(pending).rejects.toThrow(/connection/i)
        expect(Date.now() - dropped).toBeLessThan(1000)
    })
})
