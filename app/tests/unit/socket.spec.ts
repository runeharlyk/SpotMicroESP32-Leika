import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { WebSocketServer } from 'ws'
import { socket } from '../../src/lib/stores/socket'
import { IMUData, RSSIData, WebsocketMessage } from '../../src/lib/platform_shared/websocket_message'

// Helper function to create encoded WebSocket messages
function createEncodedMessage(messageType: 'imu' | 'rssi' | 'mode', data: any): Uint8Array {
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
		await new Promise<void>((resolve) => {
			wss.on('listening', () => resolve())
		})
	})

	afterEach(async () => {
		// Close all connections and server
		wss.clients.forEach((client) => client.close())
		await new Promise<void>((resolve) => {
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
		let receivedIMUData: any = null

		// Subscribe to IMU messages before connecting
		const unsubscribe = socket.on(IMUData, (data) => {
			receivedIMUData = data
		})

		// Connect socket
		socket.init(`ws://localhost:${TEST_PORT}`)

		// Wait for client to connect
		await new Promise<void>((resolve) => {
			wss.on('connection', (ws) => {
				// Server sends IMU data to client
				const imuPayload = {
					x: 1.5,
					y: 2.5,
					z: 3.5,
					temp: 25.0
				}

				const encodedMessage = createEncodedMessage('imu', imuPayload)
				ws.send(encodedMessage)

				setTimeout(resolve, 50)
			})
		})

		expect(receivedIMUData).toBeDefined()
		expect(receivedIMUData.imu?.x).toBe(1.5)
		expect(receivedIMUData.imu?.y).toBe(2.5)
		expect(receivedIMUData.imu?.z).toBe(3.5)
		expect(receivedIMUData.imu?.temp).toBe(25.0)

		unsubscribe()
	})

})

describe('WebsocketMessage Protobuf Encoding/Decoding', () => {
	it('should encode and decode IMU data correctly', () => {
		const imuData = {
			x: 1.5,
			y: 2.5,
			z: 3.5,
			temp: 25.0
		}

		const encoded = IMUData.encode(imuData).finish()
		const decoded = IMUData.decode(encoded)

		expect(decoded.x).toBe(imuData.x)
		expect(decoded.y).toBe(imuData.y)
		expect(decoded.z).toBe(imuData.z)
		expect(decoded.temp).toBe(imuData.temp)
	})

	

	it('should encode and decode complete WebsocketMessage', () => {
		const original = WebsocketMessage.create({
			imu: {
				x: 1.5,
				y: 2.5,
				z: 3.5,
				temp: 25.0
			}
		})

		const encoded = WebsocketMessage.encode(original).finish()
		const decoded = WebsocketMessage.decode(encoded)

		expect(decoded.imu).toBeDefined()
		expect(decoded.imu?.x).toBe(1.5)
		expect(decoded.imu?.y).toBe(2.5)
		expect(decoded.imu?.z).toBe(3.5)
		expect(decoded.imu?.temp).toBe(25.0)
	})

})
