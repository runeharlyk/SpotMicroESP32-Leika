import { describe, it, expect, afterEach } from 'vitest'
import { WebSocketServer } from 'ws'
import { createWebSocket, decodeMessage } from '../../src/lib/stores/socket'
import { IMUData, Message } from '../../src/lib/platform_shared/message'

// A message kind this app does not know, as a newer firmware would send: field 999, a varint.
const fromNewerFirmware = new Uint8Array([0xb8, 0x3e, 0x01])

describe.sequential('a firmware newer than the app', () => {
    let wss: WebSocketServer | undefined

    afterEach(async () => {
        if (!wss) return
        wss.clients.forEach(client => client.terminate())
        await new Promise(resolve => wss!.close(resolve))
        wss = undefined
    })

    it('decodes a message kind it does not know as nothing', () => {
        expect(decodeMessage(fromNewerFirmware.buffer)).toBeUndefined()
    })

    it('keeps delivering the messages it knows after one it does not', async () => {
        wss = new WebSocketServer({ port: 9400 })
        await new Promise(resolve => wss!.on('listening', resolve))
        wss.on('connection', client => {
            client.on('message', () => {
                client.send(fromNewerFirmware)
                client.send(Message.encode(Message.create({ imu: { x: 2 } })).finish())
            })
        })
        const socket = createWebSocket()
        const received = new Promise<IMUData>(resolve => socket.on(IMUData, resolve))
        socket.init('ws://localhost:9400')

        expect((await received).x).toBe(2)
    })
})
