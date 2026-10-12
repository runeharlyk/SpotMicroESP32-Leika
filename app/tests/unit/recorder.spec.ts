// @vitest-environment node
import { describe, it, expect, afterAll } from 'vitest'
import { get } from 'svelte/store'
import { WebSocketServer, type WebSocket } from 'ws'
import { socket, MESSAGE_KEY_TO_TAG } from '../../src/lib/stores/socket'
import { recorder } from '../../src/lib/recording/recorder'
import { readRecords } from '../../src/lib/recording/leika-recording'
import { Message, TelemetryHeader, TickSample } from '../../src/lib/platform_shared/message'

const PORT = 8931
const frame = (message: Partial<Message>) => Message.encode(Message.create(message)).finish()
const header = frame({
    telemetryHeader: TelemetryHeader.create({ firmwareVersion: 'recorder-test', imuRateHz: 200 })
})
const batch = (batchSeq: number) =>
    frame({
        telemetryBatch: {
            batchSeq,
            droppedTicks: 0,
            ticks: Array.from({ length: 10 }, (_, i) =>
                TickSample.create({ seq: batchSeq * 10 + i })
            )
        }
    })
const network = frame({
    telemetryNetwork: {
        tUs: 1,
        rssi: -60,
        channel: 1,
        batchesSent: 1,
        batchesFailed: 0,
        ticksDropped: 0
    }
})
const unrelated = frame({
    imu: { x: 1, y: 2, z: 3, heading: 0, altitude: 0, bmpTemp: 0, pressure: 0 }
})

async function until(condition: () => boolean, ms = 5000) {
    const deadline = Date.now() + ms
    while (!condition()) {
        if (Date.now() > deadline) throw new Error('timed out')
        await new Promise(resolve => setTimeout(resolve, 10))
    }
}

/**
 * Stands in for the robot's socket: like main.cpp it answers a subscription to the batches with the header, and
 * then streams what `stream` returns for that connection.
 */
function fakeRobot(stream: (connection: number) => Uint8Array[]) {
    const wss = new WebSocketServer({ port: PORT })
    const subscribed: number[] = []
    const unsubscribed: number[] = []
    const clients: WebSocket[] = []
    wss.on('connection', ws => {
        const connection = clients.push(ws) - 1
        ws.on('message', (data: Buffer) => {
            const message = Message.decode(new Uint8Array(data))
            if (message.pingmsg) ws.send(frame({ pongmsg: {} }))
            if (message.unsubNotif) unsubscribed.push(message.unsubNotif.tag)
            if (!message.subNotif) return
            subscribed.push(message.subNotif.tag)
            if (message.subNotif.tag === MESSAGE_KEY_TO_TAG.get('telemetryBatch')) {
                ws.send(header)
                stream(connection).forEach(f => ws.send(f))
            }
        })
    })
    return { wss, subscribed, unsubscribed, clients }
}

describe('the telemetry recorder', () => {
    const robot = fakeRobot(connection =>
        connection === 0 ? [batch(1), unrelated, network] : [batch(3)]
    )
    afterAll(async () => {
        robot.wss.clients.forEach(client => client.terminate())
        await new Promise(resolve => robot.wss.close(resolve))
    })

    it('keeps the robot frames as they arrived, with the header and a gap where the link dropped', async () => {
        socket.init(`ws://localhost:${PORT}`)
        await until(() => get(socket))

        recorder.start()
        await until(() => get(recorder).frames === 3)
        expect(robot.subscribed).toEqual(
            ['telemetryHeader', 'telemetryBatch', 'telemetryNetwork'].map(key =>
                MESSAGE_KEY_TO_TAG.get(key)
            )
        )

        robot.clients[0].terminate()
        await until(() => get(recorder).frames === 5)
        await recorder.stop()

        const finished = get(recorder).finished!
        const bytes = new Uint8Array(await finished.blob.arrayBuffer())
        const frames = [...readRecords(bytes)].map(([, f]) => Array.from(f))
        expect(frames).toEqual(
            [header, batch(1), network, [], header, batch(3)].map(f => Array.from(f))
        )
        expect(finished.blob.size).toBe(get(recorder).bytes)
        expect(finished.filename).toMatch(/^recording-\d{8}-\d{6}\.leika$/)
        expect(finished.recording?.header?.firmwareVersion).toBe('recorder-test')
        expect(finished.stats).toMatchObject({ ticks: 20, missing_ticks: 10, link_gaps: 1 })
        await until(() => robot.unsubscribed.length === 3)
    })
})
