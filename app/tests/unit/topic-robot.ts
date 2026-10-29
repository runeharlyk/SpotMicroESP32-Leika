import { readFileSync } from 'fs'
import { resolve } from 'path'
import { WebSocketServer, type WebSocket } from 'ws'
import {
    AnimationStatus,
    CorrelationResponse,
    GoalStatus,
    Message,
    ModeData,
    OtaStatus,
    SubscribeNotification,
    WalkGaitData,
    type CorrelationRequest,
    type MessageFns
} from '../../src/lib/platform_shared/message'
import { WifiStatus } from '../../src/lib/platform_shared/api'
import { MESSAGE_TYPE_TO_KEY, MESSAGE_TYPE_TO_TAG } from '../../src/lib/stores/socket'
import type { StateTopic, ValueOf } from '../../src/lib/transport/topics'

/** platform_shared/topics.json, which the firmware's topic table is tested against too. */
export const SHARED_TOPICS: { topics: { name: string; tag: number; kind: 'state' | 'stream' }[] } =
    JSON.parse(readFileSync(resolve(__dirname, '../../../platform_shared/topics.json'), 'utf8'))

const camel = (name: string) => name.replace(/_(\w)/g, (_, c: string) => c.toUpperCase())

// Each state topic's value at boot, keyed by its field in the Message oneof: the idle values the firmware reports.
const BOOT_VALUES: Record<string, unknown> = {
    mode: ModeData.create(),
    walkGait: WalkGaitData.create(),
    wifiStatus: WifiStatus.create(),
    animationStatus: AnimationStatus.create(),
    goalStatus: GoalStatus.create(),
    otaStatus: OtaStatus.create()
}

export type Responder = (
    request: CorrelationRequest
) => Partial<Omit<CorrelationResponse, 'correlationId'>>

const tagOf = (fns: MessageFns<unknown>) => MESSAGE_TYPE_TO_TAG.get(fns)!
const keyOf = (fns: MessageFns<unknown>) => MESSAGE_TYPE_TO_KEY.get(fns)!

/**
 * The robot on a WebSocket on localhost, answering as the firmware does (esp32/test/host/topic_snapshot_test.cpp): a
 * subscribe to a state topic with the topic's current value and then the subscribe echoed as the ack, a subscribe to a
 * stream with the ack alone, and a repeated subscribe again without doubling the pushes. Every state topic of
 * platform_shared/topics.json holds a value from the start, or construction fails, as the firmware's table does without
 * a snapshot. A request goes to its responder, and without one is answered 501. Pings get pongs.
 */
export class TopicRobot {
    private readonly values = new Map<number, { key: string; value: unknown }>()
    private readonly clients = new Map<WebSocket, Set<number>>()
    private readonly responders = new Map<string, Responder>()
    private readonly dropping = new Map<number, number>()
    private acking = true
    private stalled = false
    private server: WebSocketServer | undefined
    /** Every frame the robot received, decoded, in order. */
    readonly received: Message[] = []

    constructor() {
        for (const { name, kind, tag } of SHARED_TOPICS.topics) {
            if (kind !== 'state') continue
            const key = camel(name)
            if (!(key in BOOT_VALUES))
                throw new Error(`the topic robot has no value for state topic ${name}`)
            this.values.set(tag, { key, value: BOOT_VALUES[key] })
        }
    }

    /** Listens on a free port; the robot's address, for the socket's init. */
    async start(): Promise<string> {
        const server = new WebSocketServer({ port: 0 })
        this.server = server
        await new Promise(resolve => server.on('listening', resolve))
        server.on('connection', client => {
            this.clients.set(client, new Set())
            client.on('message', raw => this.receive(client, new Uint8Array(raw as Buffer)))
            client.on('close', () => this.clients.delete(client))
        })
        const address = server.address()
        if (typeof address === 'string' || address === null) throw new Error('no port')
        return `ws://localhost:${address.port}`
    }

    async stop() {
        this.clients.forEach((_, client) => client.terminate())
        await new Promise(resolve => this.server?.close(resolve))
    }

    /** Ends every connection, as a robot that restarts or walks out of range does. */
    drop() {
        this.clients.forEach((_, client) => client.terminate())
    }

    /** A state topic changes: stored, and pushed to its subscribers. */
    set<F extends StateTopic>(topic: F, value: ValueOf<F>) {
        const fns = topic as MessageFns<unknown>
        const key = keyOf(fns)
        this.values.set(tagOf(fns), { key, value })
        this.pushToSubscribers(tagOf(fns), Message.create({ [key]: value }))
    }

    respond(key: keyof CorrelationRequest, responder: Responder) {
        this.responders.set(key, responder)
    }

    /** The next `frames` frames of a tag, snapshots or pushes, are lost on their way. */
    dropNext(tag: number, frames = 1) {
        this.dropping.set(tag, (this.dropping.get(tag) ?? 0) + frames)
    }

    /** Behaves as firmware from before the state topics: a subscribe is recorded and answered with nothing. */
    ackless() {
        this.acking = false
    }

    /** Stops answering anything. */
    stall() {
        this.stalled = true
    }

    /** The tags subscribed to, in the order the subscribes arrived, repeats included. */
    subscriptions(): number[] {
        return this.received.flatMap(m => (m.subNotif ? [m.subNotif.tag] : []))
    }

    private receive(client: WebSocket, bytes: Uint8Array) {
        const message = Message.decode(bytes)
        this.received.push(message)
        const subscribed = this.clients.get(client)
        if (this.stalled || !subscribed) return
        if (message.subNotif) {
            const tag = message.subNotif.tag
            subscribed.add(tag)
            if (!this.acking) return
            const state = this.values.get(tag)
            if (state) this.send(client, tag, Message.create({ [state.key]: state.value }))
            this.send(
                client,
                null,
                Message.create({ subNotif: SubscribeNotification.create({ tag }) })
            )
        } else if (message.unsubNotif) {
            subscribed.delete(message.unsubNotif.tag)
        } else if (message.pingmsg) {
            this.send(client, null, Message.create({ pongmsg: {} }))
        } else if (message.correlationRequest) {
            const request = message.correlationRequest
            const key = Object.keys(request).find(
                k => k !== 'correlationId' && request[k as keyof CorrelationRequest] !== undefined
            )
            const answer = (key && this.responders.get(key)?.(request)) || { statusCode: 501 }
            const response = CorrelationResponse.create({
                correlationId: request.correlationId,
                statusCode: 200,
                ...answer
            })
            this.send(client, null, Message.create({ correlationResponse: response }))
        }
    }

    private pushToSubscribers(tag: number, message: Message) {
        for (const [client, tags] of this.clients)
            if (tags.has(tag)) this.send(client, tag, message)
    }

    private send(client: WebSocket, tag: number | null, message: Message) {
        const dropping = tag === null ? 0 : (this.dropping.get(tag) ?? 0)
        if (dropping > 0) return void this.dropping.set(tag!, dropping - 1)
        client.send(Message.encode(message).finish())
    }
}
