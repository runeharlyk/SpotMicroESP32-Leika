import { describe, it, expect, afterEach } from 'vitest'
import { WebSocketServer, type WebSocket } from 'ws'
import { NOT_CONNECTED, createWebSocket } from '../../src/lib/stores/socket'
import { CorrelationResponse, Message } from '../../src/lib/platform_shared/message'
import { ServoSettings } from '../../src/lib/platform_shared/api'

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

    // A robot that answers every request with 200, recording which requests reached it.
    async function answeringRobot() {
        const received: string[] = []
        const url = await silentRobot(client =>
            client.on('message', (frame: Buffer) => {
                const request = Message.decode(new Uint8Array(frame)).correlationRequest
                if (!request) return
                received.push(
                    Object.entries(request).find(
                        ([name, value]) => name !== 'correlationId' && value !== undefined
                    )![0]
                )
                const response = CorrelationResponse.create({
                    correlationId: request.correlationId,
                    statusCode: 200
                })
                client.send(
                    Message.encode(Message.create({ correlationResponse: response })).finish()
                )
            })
        )
        return { url, received }
    }

    // An action moves the robot or ends something under way; by the time a link comes, it may no
    // longer be what the user wants.
    it('fails an action at once while no link is up, and never sends it later', async () => {
        const robot = await answeringRobot()
        const socket = createWebSocket({ requestTimeoutTime: 2000 })

        await expect(socket.request({ imuCalibrateExecute: {} })).rejects.toThrow(NOT_CONNECTED)

        socket.init(robot.url)
        await socket.request({ featuresDataRequest: {} })
        expect(robot.received).toEqual(['featuresDataRequest'])
    })

    it('holds a settings write through a link blip', async () => {
        const robot = await answeringRobot()
        const socket = createWebSocket({ requestTimeoutTime: 2000, settingGraceTime: 1000 })

        const write = socket.request({ servoSettings: ServoSettings.create() })
        socket.init(robot.url)

        await expect(write).resolves.toMatchObject({ statusCode: 200 })
        expect(robot.received).toEqual(['servoSettings'])
    })

    it('fails a settings write as not saved once its grace passed, and never sends it', async () => {
        const robot = await answeringRobot()
        const socket = createWebSocket({ requestTimeoutTime: 2000, settingGraceTime: 200 })

        await expect(socket.request({ servoSettings: ServoSettings.create() })).rejects.toThrow(
            NOT_CONNECTED
        )

        socket.init(robot.url)
        await socket.request({ featuresDataRequest: {} })
        expect(robot.received).toEqual(['featuresDataRequest'])
    })

    it('times a read held for a link out from the call, not from when the link came', async () => {
        const url = await silentRobot()
        const socket = createWebSocket({ requestTimeoutTime: 1000 })
        const started = Date.now()

        const read = socket.request({ systemInformationRequest: {} })
        setTimeout(() => socket.init(url), 700)

        await expect(read).rejects.toThrow(/timeout/i)
        expect(Date.now() - started).toBeLessThan(1400)
    })
})
