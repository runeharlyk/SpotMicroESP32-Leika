import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { WebSocketServer } from 'ws'
import { decodeMessage, MESSAGE_KEY_TO_TAG, socket } from '../../src/lib/stores/socket'
import {
    IMUData,
    PingMsg,
    PongMsg,
    WebsocketMessage
} from '../../src/lib/platform_shared/websocket_message'

// Helper function to create encoded WebSocket messages
function createEncodedMessage(messageType: 'imu' | 'rssi' | 'mode', data: unknown): Uint8Array {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const message: any = {}
    message[messageType] = data
    const wsMessage = WebsocketMessage.create(message)
    return WebsocketMessage.encode(wsMessage).finish()
}

describe.sequential('WebSocket Integration Tests', () => {
    let wss: WebSocketServer
    let TEST_PORT = 8765

    beforeEach(async () => {
        // Use a different port for each test to avoid conflicts
        TEST_PORT++

        // Create real WebSocket server
        wss = new WebSocketServer({ port: TEST_PORT })

        // Wait for server to start
        await new Promise<void>(resolve => {
            wss.on('listening', () => resolve())
        })
    })

    afterEach(async () => {
        // Close all connections and server
        wss.clients.forEach(client => client.close())
        await new Promise<void>(resolve => {
            wss.close(() => resolve())
        })
        // Wait a bit for cleanup
        await new Promise(resolve => setTimeout(resolve, 100))
    })

    it('should connect to WebSocket server', async () => {
        socket.init(`ws://localhost:${TEST_PORT}`)

        // Wait for connection
        await new Promise(resolve => setTimeout(resolve, 100))

        let isConnected = false
        socket.subscribe(value => {
            isConnected = value
        })()

        expect(isConnected).toBe(true)
    })

    it('should receive and decode IMU data from server', async () => {
        let receivedIMUData: IMUData = null

        // Subscribe to IMU messages before connecting
        const unsubscribe = socket.on(IMUData, data => {
            receivedIMUData = data
        })

        // Connect socket
        socket.init(`ws://localhost:${TEST_PORT}`)

        // Wait for client to connect
        await new Promise<void>(resolve => {
            wss.on('connection', ws => {
                // Server sends IMU data to client
                const imuPayload = IMUData.create({
                    x: 3.25,
                    y: 2.5,
                    z: 1.75,
                    heading: 10,
                    altitude: 11,
                    bmpTemp: 22,
                    pressure: 23
                })

                const encodedMessage = createEncodedMessage('imu', imuPayload)
                ws.send(encodedMessage)

                setTimeout(resolve, 50)
            })
        })

        expect(receivedIMUData).toBeDefined()

        expect(receivedIMUData.x).toBe(3.25)
        expect(receivedIMUData.y).toBe(2.5)
        expect(receivedIMUData.z).toBe(1.75)
        expect(receivedIMUData.heading).toBe(10)
        expect(receivedIMUData.altitude).toBe(11)
        expect(receivedIMUData.bmpTemp).toBe(22)
        expect(receivedIMUData.pressure).toBe(23)

        unsubscribe()
    })

    it('should send IMU data from client to server using sendEvent', async () => {
        let serverReceivedData: any = null

        // Connect socket
        socket.init(`ws://localhost:${TEST_PORT}`)

        // Wait for client to connect and send data
        await new Promise<void>((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Test timeout - server did not receive message'))
            }, 3000)

            wss.on('connection', ws => {
                // console.log('Server: Client connected')

                // Server listens for messages from client
                ws.on('message', (data: Buffer) => {
                    // console.log('Server: Received message, length:', data.length)

                    // Skip empty messages (from ping, etc.)
                    if (data.length === 0) {
                        console.log('Server: Skipping empty message (Probably a ping')
                        return
                    }

                    try {
                        // Decode the protobuf message
                        const decoded = WebsocketMessage.decode(new Uint8Array(data))
                        // console.log('Server: Decoded message:', JSON.stringify(decoded, null, 2))

                        // Only resolve if we got actual IMU data
                        if (decoded.imu) {
                            serverReceivedData = decoded
                            clearTimeout(timeout)
                            resolve()
                        } else {
                            // console.log('Server: Message decoded but no IMU data, waiting...')
                        }
                    } catch (error) {
                        console.error('Server: Failed to decode:', error)
                        clearTimeout(timeout)
                        reject(error)
                    }
                })
            })

            // Wait for WebSocket to be fully connected
            setTimeout(() => {
                console.log('Client: Sending IMU data...')
                // Client sends IMU data to server
                const imuData = IMUData.create({
                    x: 3.25,
                    y: 2.5,
                    z: 1.75,
                    heading: 10,
                    altitude: 11,
                    bmpTemp: 22,
                    pressure: 23
                })
                socket.sendEvent(IMUData, imuData)
                console.log('Client: sendEvent called')
            }, 150)
        })

        // Verify server received the data
        expect(serverReceivedData).toBeDefined()
        expect(serverReceivedData?.imu).toBeDefined()

        expect(serverReceivedData?.imu.x).toBe(3.25)
        expect(serverReceivedData?.imu.y).toBe(2.5)
        expect(serverReceivedData?.imu.z).toBe(1.75)
        expect(serverReceivedData?.imu.heading).toBe(10)
        expect(serverReceivedData?.imu.altitude).toBe(11)
        expect(serverReceivedData?.imu.bmpTemp).toBe(22)
        expect(serverReceivedData?.imu.pressure).toBe(23)
    })

    it('should fail to serialize data on sendEvent', async () => {
        // Connect socket
        socket.init(`ws://localhost:${TEST_PORT}`)

        await new Promise<void>((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Test timeout'))
            }, 1000)

            // Wait for WebSocket to be fully connected
            setTimeout(() => {
                console.log('Client: Sending invalid message type...')
                // Send any invalid message type
                const wsm = WebsocketMessage.create()
                try {
                    socket.sendEvent(WebsocketMessage as any, wsm)
                    clearTimeout(timeout)
                    reject(new Error('Expected sendEvent to throw, but it did not'))
                } catch (e) {
                    console.log('Client: sendEvent correctly threw error:', e)
                    clearTimeout(timeout)
                    resolve()
                }
            }, 150)
        })
    })
})

describe('WebsocketMessage Protobuf Encoding/Decoding', () => {
    it('should encode and decode IMU data correctly', () => {
        const imuData = IMUData.create({
            x: 3.25,
            y: 2.5,
            z: 1.75,
            heading: 10,
            altitude: 11,
            bmpTemp: 22,
            pressure: 23
        })

        const encoded = IMUData.encode(imuData).finish()
        const decoded = IMUData.decode(encoded)

        expect(decoded.x).toBe(3.25)
        expect(decoded.y).toBe(2.5)
        expect(decoded.z).toBe(1.75)
        expect(decoded.heading).toBe(10)
        expect(decoded.altitude).toBe(11)
        expect(decoded.bmpTemp).toBe(22)
        expect(decoded.pressure).toBe(23)
    })

    it('should encode and decode two empty types correctly', () => {
        const encoded_ping = WebsocketMessage.encode(
            WebsocketMessage.create({ pingmsg: PingMsg.create() })
        ).finish()
        const decoded_ping = decodeMessage(encoded_ping.buffer)
        expect(decoded_ping.tag).toBe(MESSAGE_KEY_TO_TAG.get('pingmsg'))

        const encoded_pong = WebsocketMessage.encode(
            WebsocketMessage.create({ pongmsg: PongMsg.create() })
        ).finish()
        const decoded_pong = decodeMessage(encoded_pong.buffer)
        expect(decoded_pong.tag).toBe(MESSAGE_KEY_TO_TAG.get('pongmsg'))
    })

    it('should encode and decode complete WebsocketMessage', () => {
        const original = WebsocketMessage.create({
            imu: IMUData.create({
                x: 3.25,
                y: 2.5,
                z: 1.75,
                heading: 10,
                altitude: 11,
                bmpTemp: 22,
                pressure: 23
            })
        })

        const encoded = WebsocketMessage.encode(original).finish()
        const decoded = WebsocketMessage.decode(encoded)

        expect(decoded.imu).toBeDefined()
        expect(decoded.imu?.x).toBe(3.25)
        expect(decoded.imu?.y).toBe(2.5)
        expect(decoded.imu?.z).toBe(1.75)
        expect(decoded.imu?.heading).toBe(10)
        expect(decoded.imu?.altitude).toBe(11)
        expect(decoded.imu?.bmpTemp).toBe(22)
        expect(decoded.imu?.pressure).toBe(23)
    })
})
