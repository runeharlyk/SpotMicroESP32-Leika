import { afterEach, describe, expect, it, vi } from 'vitest'
import { WebSocketServer, type WebSocket } from 'ws'
import { createWebSocket } from '../../src/lib/stores/socket'
import { AnimationStatus, Message } from '../../src/lib/platform_shared/message'

// A page that unmounts drops its stream, and one that mounts again must get it back: the robot sends a
// stream only to clients subscribed to it.
describe.sequential('stream subscriptions', () => {
    let wss: WebSocketServer | undefined

    afterEach(async () => {
        if (!wss) return
        wss.clients.forEach(client => client.terminate())
        await new Promise(resolve => wss!.close(resolve))
        wss = undefined
    })

    it('subscribes again when a listener returns after the last one left', async () => {
        wss = new WebSocketServer({ port: 9407 })
        await new Promise(resolve => wss!.on('listening', resolve))
        const seen: string[] = []
        const connected = new Promise<WebSocket>(resolve => wss!.on('connection', resolve))
        wss.on('connection', client =>
            client.on('message', raw => {
                const m = Message.decode(new Uint8Array(raw as Buffer))
                if (m.subNotif) seen.push(`sub ${m.subNotif.tag}`)
                if (m.unsubNotif) seen.push(`unsub ${m.unsubNotif.tag}`)
            })
        )
        const socket = createWebSocket()
        socket.init('ws://localhost:9407')
        await connected

        const first = socket.on(AnimationStatus, () => {})
        await vi.waitFor(() => expect(seen).toEqual(['sub 292']))
        first()
        await vi.waitFor(() => expect(seen).toEqual(['sub 292', 'unsub 292']))
        socket.on(AnimationStatus, () => {})
        await vi.waitFor(() => expect(seen).toEqual(['sub 292', 'unsub 292', 'sub 292']))
    })
})
