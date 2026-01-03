import { writable } from 'svelte/store'
import {
    WebsocketMessage,
    type MessageFns,
    protoMetadata as websocket_md
} from '$lib/platform_shared/websocket_message'
import * as WebsocketMessages from '$lib/platform_shared/websocket_message'

export const MESSAGE_TYPE_TO_KEY = new Map<MessageFns<unknown>, string>()
export const MESSAGE_TYPE_TO_TAG = new Map<MessageFns<unknown>, number>()
export const MESSAGE_KEY_TO_TAG = new Map<string, number>()

const websocketMessageType = websocket_md.fileDescriptor.messageType?.find(
    (msg: { name: string }) => msg.name === 'WebsocketMessage'
)

if (websocketMessageType?.field) {
    for (const field of websocketMessageType.field) {
        if (field.typeName) {
            const messageFns = websocket_md.references[field.typeName]
            if (messageFns && field.jsonName && field.number) {
                MESSAGE_TYPE_TO_KEY.set(messageFns, field.jsonName)
                MESSAGE_TYPE_TO_TAG.set(messageFns, field.number)
                MESSAGE_KEY_TO_TAG.set(field.jsonName, field.number)
            }
        }
    }
}

function get_name_from_messagetype<T>(event_type: MessageFns<T>): string {
    const event = MESSAGE_TYPE_TO_KEY.get(event_type as MessageFns<unknown>)
    if (!event) {
        throw new Error(
            "Event type not found in 'WebsocketMessage'. The MessageFns you passed doesn't correspond to any WebsocketMessage field."
        )
    }
    return event
}

function get_tag_from_messagetype<T>(event_type: MessageFns<T>): number {
    const fieldNumber = MESSAGE_TYPE_TO_TAG.get(event_type as MessageFns<unknown>)
    if (fieldNumber === undefined) {
        throw new Error(
            "Tag not found in 'WebsocketMessage'. The MessageFns you passed doesn't correspond to any WebsocketMessage field."
        )
    }
    return fieldNumber
}

type SocketEvent = 'open' | 'close' | 'error' | 'message' | 'unresponsive'

type TaggedSocketMessage = { tag: number; msg: WebsocketMessage }

export const decodeMessage = (data: ArrayBuffer): TaggedSocketMessage => {
    const decoded = WebsocketMessage.decode(new Uint8Array(data))
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

export const encodeMessage = (data: WebsocketMessage): Uint8Array<ArrayBuffer> => {
    const encoded = WebsocketMessage.encode(data).finish()
    return encoded
}

function createWebSocket() {
    const message_listeners = new Map<number, Set<(data?: unknown) => void>>()
    const event_listeners = new Map<string, Set<(data?: unknown) => void>>()
    const { subscribe, set } = writable(false)
    const reconnectTimeoutTime = 5000
    let unresponsiveTimeoutId: ReturnType<typeof setTimeout>
    let reconnectTimeoutId: ReturnType<typeof setTimeout>
    let ws: WebSocket
    let socketUrl: string | URL

    function init(url: string | URL) {
        socketUrl = url
        connect()
    }

    function disconnect(reason: SocketEvent, event?: Event) {
        ws.close()
        set(false)
        clearTimeout(unresponsiveTimeoutId)
        clearTimeout(reconnectTimeoutId)
        event_listeners.get(reason)?.forEach(listener => listener(event))
        reconnectTimeoutId = setTimeout(connect, reconnectTimeoutTime)
    }

    function connect() {
        ws = new WebSocket(socketUrl)
        ws.binaryType = 'arraybuffer'
        ws.onopen = ev => {
            ping()
            set(true)
            clearTimeout(reconnectTimeoutId)
            resubscribeAll()
            event_listeners.get('open')?.forEach(listener => listener(ev))
        }
        ws.onmessage = frame => {
            resetUnresponsiveCheck()
            const { tag, msg } = decodeMessage(frame.data)
            if (tag) message_listeners.get(tag)?.forEach(listener => listener(msg))
        }
        ws.onerror = ev => disconnect('error', ev)
        ws.onclose = ev => disconnect('close', ev)
    }

    function unsubscribe<MT>(event_type: MessageFns<MT>, listener: (data: MT) => void) {
        const tag = get_tag_from_messagetype(event_type)
        const message_listeners_totag = message_listeners.get(tag)
        if (!message_listeners_totag) return

        message_listeners_totag?.delete(listener as (data?: unknown) => void)
        if (message_listeners_totag.size == 0) {
            unsubscribeToMessageFromServer(event_type)
        }
    }

    function unsubscribe_event(event_type: SocketEvent, listener: (data: unknown) => void) {
        const message_listeners_totag = event_listeners.get(event_type)
        if (!message_listeners_totag) return

        message_listeners_totag?.delete(listener)
    }

    function resetUnresponsiveCheck() {
        clearTimeout(unresponsiveTimeoutId)
        unresponsiveTimeoutId = setTimeout(() => disconnect('unresponsive'), reconnectTimeoutTime)
    }

    function sendEvent<T>(event: MessageFns<T>, data: T) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        const type = get_name_from_messagetype(event)
        const wsm = WebsocketMessage.create() as Record<string, unknown>
        wsm[type] = data
        send(wsm as WebsocketMessage)
    }

    function unsubscribeToMessageFromServer<T>(event_type: MessageFns<T>) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        const unsub_msg = WebsocketMessages.UnsubscribeNotification.create({
            tag: get_tag_from_messagetype(event_type)
        })
        send(WebsocketMessage.create({ unsubNotif: unsub_msg }))
    }

    function subscribeToEvent<T>(event_type: MessageFns<T>) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        const sub_msg = WebsocketMessages.SubscribeNotification.create({
            tag: get_tag_from_messagetype(event_type)
        })
        send(WebsocketMessage.create({ subNotif: sub_msg }))
    }

    function resubscribeAll() {
        for (const tag of message_listeners.keys()) {
            const sub_msg = WebsocketMessages.SubscribeNotification.create({ tag })
            send(WebsocketMessage.create({ subNotif: sub_msg }))
        }
    }

    function send(data: WebsocketMessage) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        const encoded = encodeMessage(data)
        ws.send(encoded)
    }

    function ping() {
        send(WebsocketMessage.create({ pingmsg: {} }))
    }

    return {
        subscribe,
        sendEvent,
        init,
        on: <MT>(event_type: MessageFns<MT>, listener: (data: MT) => void): (() => void) => {
            const tag = get_tag_from_messagetype(event_type)

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
                unsubscribe_event(event_type, listener)
            }
        }
    }
}

export const socket = createWebSocket()
