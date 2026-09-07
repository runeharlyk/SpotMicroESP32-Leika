import { writable } from 'svelte/store'
import {
    Message,
    CorrelationRequest,
    CorrelationResponse,
    protoMetadata,
    type MessageFns
} from '$lib/platform_shared/message'
import * as Messages from '$lib/platform_shared/message'
import { protoMetadata as filesystemProtoMetadata } from '$lib/platform_shared/filesystem'
import { telemetry } from './telemetry'
import type { ITransport, TransportCloseReason } from '$lib/transport/transport.interface'
import { createWebSocketTransport } from '$lib/transport/websocket-adapter'
import { createBleTransport } from '$lib/transport/ble-adapter'

export const MESSAGE_TYPE_TO_KEY = new Map<MessageFns<unknown>, string>()
export const MESSAGE_TYPE_TO_TAG = new Map<MessageFns<unknown>, number>()
export const MESSAGE_KEY_TO_TAG = new Map<string, number>()
export const MESSAGE_TAG_TO_KEY = new Map<number, string>()

type CorrelationRequestData = Omit<CorrelationRequest, 'correlationId'>
type PendingRequest = {
    resolve: (response: CorrelationResponse) => void
    reject: (error: Error) => void
    timeoutId: ReturnType<typeof setTimeout>
}

// Every proto file contributing types to the Message envelope; a missing one silently
// drops those types from the maps below.
const combinedReferences: Record<string, MessageFns<unknown>> = {
    ...protoMetadata.references,
    ...filesystemProtoMetadata.references
}

const MessageType = protoMetadata.fileDescriptor.messageType?.find(
    (msg: { name: string }) => msg.name === 'Message'
)

if (MessageType?.field) {
    for (const field of MessageType.field) {
        if (field.typeName) {
            const messageFns = combinedReferences[field.typeName]
            if (messageFns && field.jsonName && field.number) {
                MESSAGE_TYPE_TO_KEY.set(messageFns, field.jsonName)
                MESSAGE_TYPE_TO_TAG.set(messageFns, field.number)
                MESSAGE_KEY_TO_TAG.set(field.jsonName, field.number)
                MESSAGE_TAG_TO_KEY.set(field.number, field.jsonName)
            }
        }
    }
}

function getNameFromMessageType<T>(event_type: MessageFns<T>): string {
    const event = MESSAGE_TYPE_TO_KEY.get(event_type as MessageFns<unknown>)
    if (!event) {
        throw new Error(
            "Event type not found in 'Message'. The MessageFns you passed doesn't correspond to any Message field."
        )
    }
    return event
}

function getTagFromMessageType<T>(event_type: MessageFns<T>): number {
    const fieldNumber = MESSAGE_TYPE_TO_TAG.get(event_type as MessageFns<unknown>)
    if (fieldNumber === undefined) {
        throw new Error(
            "Tag not found in 'Message'. The MessageFns you passed doesn't correspond to any Message field."
        )
    }
    return fieldNumber
}

type SocketEvent = 'open' | 'close' | 'error' | 'message' | 'unresponsive'

type TaggedMessage = { tag: number; msg: Message }

export const decodeMessage = (data: ArrayBuffer): TaggedMessage => {
    const decoded = Message.decode(new Uint8Array(data))
    const values = Object.entries(decoded).filter(([, value]) => value !== undefined)
    if (values.length != 1) {
        throw new Error('Message included either 0 or more than 1 data point')
    }
    const fieldName = values[0][0]
    const tag = MESSAGE_KEY_TO_TAG.get(fieldName)
    if (tag === undefined) {
        throw new Error(`Tag not found for field: ${fieldName}`)
    }
    return { tag: tag, msg: decoded }
}

export const encodeMessage = (data: Message): Uint8Array<ArrayBuffer> => {
    const encoded = Message.encode(data).finish()
    return encoded
}

function createWebSocket() {
    const message_listeners = new Map<number, Set<(data?: unknown) => void>>()
    const event_listeners = new Map<string, Set<(data?: unknown) => void>>()
    const pending_requests = new Map<number, PendingRequest>()
    const queued_requests = new Map<
        string,
        {
            data: CorrelationRequestData
            resolve: (r: CorrelationResponse) => void
            reject: (e: Error) => void
            timeoutId: ReturnType<typeof setTimeout>
        }
    >()
    const { subscribe, set } = writable(false)
    const reconnectBaseDelay = 1000
    const reconnectMaxDelay = 10000
    const pingIntervalTime = 4000
    const unresponsiveTimeoutTime = 12000
    const requestTimeoutTime = 30000
    let reconnectAttempts = 0
    let lastPingSentAt = 0
    let correlationIdCounter = 0
    let unresponsiveTimeoutId: ReturnType<typeof setTimeout>
    let reconnectTimeoutId: ReturnType<typeof setTimeout>
    let pingIntervalId: ReturnType<typeof setInterval>
    let transport: ITransport | undefined
    let socketUrl: string | URL
    const activeTransport = writable<ITransport['kind'] | null>(null)

    const isOpen = () => transport?.isConnected() === true

    function getRequestKey(data: CorrelationRequestData): string {
        return (
            Object.keys(data).find(k => data[k as keyof CorrelationRequestData] !== undefined) ??
            'unknown'
        )
    }

    function init(url: string | URL) {
        socketUrl = url
        connect()
    }

    /**
     * Web Bluetooth needs a user gesture, so this is called from a click rather than on mount.
     * BLE carries the same protobuf envelope as the WebSocket (robot_comm_ble attaches to the same
     * broker), but only messages that fit in one ATT frame.
     */
    async function connectBluetooth() {
        clearTimeout(reconnectTimeoutId)
        transport?.close()
        transport = createBleTransport(handlers)
        try {
            await transport.connect()
        } catch (error) {
            // Pairing was cancelled or failed. Fall back to WiFi, or a dead BLE transport would be
            // left in place, whose canAutoReconnect: false also suppresses the WebSocket retry.
            if (socketUrl) connect()
            throw error
        }
    }

    function disconnect(reason: SocketEvent, event?: unknown) {
        transport?.close()
        set(false)
        activeTransport.set(null)
        clearTimeout(unresponsiveTimeoutId)
        clearTimeout(reconnectTimeoutId)
        clearInterval(pingIntervalId)
        event_listeners.get(reason)?.forEach(listener => listener(event))
        // Re-pairing a BLE device requires a user gesture, so only WiFi redials itself.
        if (transport && !transport.canAutoReconnect) return
        const delay = Math.min(reconnectBaseDelay * 2 ** reconnectAttempts, reconnectMaxDelay)
        reconnectAttempts++
        reconnectTimeoutId = setTimeout(connect, delay)
    }

    const handlers = {
        onOpen: () => {
            reconnectAttempts = 0
            set(true)
            activeTransport.set(transport?.kind ?? null)
            ping()
            clearTimeout(reconnectTimeoutId)
            clearInterval(pingIntervalId)
            pingIntervalId = setInterval(ping, pingIntervalTime)
            resetUnresponsiveCheck()
            resubscribeAll()
            flushQueuedRequests()
            event_listeners.get('open')?.forEach(listener => listener(undefined))
        },
        onClose: (reason: TransportCloseReason, event?: unknown) => disconnect(reason, event),
        onData: (data: ArrayBuffer) => {
            resetUnresponsiveCheck()

            for (const [correlationId, pending] of pending_requests) {
                clearTimeout(pending.timeoutId)
                pending.timeoutId = setTimeout(() => {
                    pending_requests.delete(correlationId)
                    pending.reject(new Error(`Request timeout (id: ${correlationId})`))
                }, requestTimeoutTime)
            }

            const { tag, msg } = decodeMessage(data)
            if (msg.pongmsg !== undefined) {
                if (lastPingSentAt > 0) telemetry.setLatency(Date.now() - lastPingSentAt)
                return
            }
            if (msg.correlationResponse) {
                const pending = pending_requests.get(msg.correlationResponse.correlationId)
                if (pending) {
                    clearTimeout(pending.timeoutId)
                    pending_requests.delete(msg.correlationResponse.correlationId)
                    pending.resolve(msg.correlationResponse)
                }
                return
            }
            if (tag) {
                const key = MESSAGE_TAG_TO_KEY.get(tag)!
                message_listeners
                    .get(tag)
                    ?.forEach(listener => listener(msg[key as keyof typeof msg]))
            }
        }
    }

    function connect() {
        transport = createWebSocketTransport(socketUrl, handlers)
        transport.connect()
    }

    function unsubscribe<MT>(event_type: MessageFns<MT>, listener: (data: MT) => void) {
        const tag = getTagFromMessageType(event_type)
        const message_listeners_totag = message_listeners.get(tag)
        if (!message_listeners_totag) return

        message_listeners_totag?.delete(listener as (data?: unknown) => void)
        if (message_listeners_totag.size == 0) {
            unsubscribeToMessageFromServer(event_type)
        }
    }

    function unsubscribeEvent(event_type: SocketEvent, listener: (data: unknown) => void) {
        const message_listeners_totag = event_listeners.get(event_type)
        if (!message_listeners_totag) return

        message_listeners_totag?.delete(listener)
    }

    function resetUnresponsiveCheck() {
        clearTimeout(unresponsiveTimeoutId)
        unresponsiveTimeoutId = setTimeout(
            () => disconnect('unresponsive'),
            unresponsiveTimeoutTime
        )
    }

    function emit<T>(event: MessageFns<T>, data: T) {
        if (!isOpen()) return
        const type = getNameFromMessageType(event)
        const wsm = Message.create() as Record<string, unknown>
        wsm[type] = data
        send(wsm as Message)
    }

    function unsubscribeToMessageFromServer<T>(event_type: MessageFns<T>) {
        if (!isOpen()) return
        const unsub_msg = Messages.UnsubscribeNotification.create({
            tag: getTagFromMessageType(event_type)
        })
        send(Message.create({ unsubNotif: unsub_msg }))
    }

    function subscribeToEvent<T>(event_type: MessageFns<T>) {
        if (!isOpen()) return
        const sub_msg = Messages.SubscribeNotification.create({
            tag: getTagFromMessageType(event_type)
        })
        send(Message.create({ subNotif: sub_msg }))
    }

    function resubscribeAll() {
        for (const tag of message_listeners.keys()) {
            const sub_msg = Messages.SubscribeNotification.create({ tag })
            send(Message.create({ subNotif: sub_msg }))
        }
    }

    function send(data: Message) {
        if (!isOpen()) return
        const encoded = encodeMessage(data)
        transport?.send(encoded)
    }

    function ping() {
        lastPingSentAt = Date.now()
        send(Message.create({ pingmsg: {} }))
    }

    function request(
        data: CorrelationRequestData,
        resolve: (r: CorrelationResponse) => void,
        reject: (e: Error) => void
    ) {
        const correlationId = ++correlationIdCounter
        const timeoutId = setTimeout(() => {
            pending_requests.delete(correlationId)
            reject(new Error(`Request timeout (id: ${correlationId})`))
        }, requestTimeoutTime)

        pending_requests.set(correlationId, { resolve, reject, timeoutId })

        const request = CorrelationRequest.create({ correlationId, ...data })
        send(Message.create({ correlationRequest: request }))
    }

    function flushQueuedRequests() {
        for (const [, { data, resolve, reject, timeoutId }] of queued_requests) {
            clearTimeout(timeoutId)
            request(data, resolve, reject)
        }
        queued_requests.clear()
    }

    return {
        subscribe,
        emit,
        init,
        connectBluetooth,
        transport: { subscribe: activeTransport.subscribe },
        on: <MT>(event_type: MessageFns<MT>, listener: (data: MT) => void): (() => void) => {
            const tag = getTagFromMessageType(event_type)

            let message_listeners_totag = message_listeners.get(tag)
            if (!message_listeners_totag) {
                message_listeners_totag = new Set()
                message_listeners.set(tag, message_listeners_totag)
                subscribeToEvent(event_type)
            }
            message_listeners_totag.add(listener as (data: unknown) => void)

            return () => {
                unsubscribe(event_type, listener)
            }
        },
        onEvent: (event_type: SocketEvent, listener: (data: unknown) => void): (() => void) => {
            let listeners = event_listeners.get(event_type)
            if (!listeners) {
                listeners = new Set()
                event_listeners.set(event_type, listeners)
            }
            listeners.add(listener)
            return () => {
                unsubscribeEvent(event_type, listener)
            }
        },
        request: (data: CorrelationRequestData): Promise<CorrelationResponse> => {
            return new Promise((resolve, reject) => {
                if (isOpen()) {
                    request(data, resolve, reject)
                } else {
                    const key = getRequestKey(data)
                    const existing = queued_requests.get(key)
                    if (existing) {
                        clearTimeout(existing.timeoutId)
                        existing.reject(new Error('Request superseded by newer request'))
                    }
                    // A queued request must expire too, or a request issued while disconnected
                    // never settles and its caller waits forever.
                    const timeoutId = setTimeout(() => {
                        queued_requests.delete(key)
                        reject(new Error(`Request timeout while disconnected (${key})`))
                    }, requestTimeoutTime)
                    queued_requests.set(key, { data, resolve, reject, timeoutId })
                }
            })
        }
    }
}

export const socket = createWebSocket()
