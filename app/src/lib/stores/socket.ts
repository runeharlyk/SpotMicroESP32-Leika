import { writable } from 'svelte/store'
import { encode, decode } from '@msgpack/msgpack'
import { WebsocketMessage, type MessageFns, protoMetadata as websocket_md } from '$lib/platform_shared/websocket_message'
import * as WebsocketMessages from '$lib/platform_shared/websocket_message'
import type { BinaryWriter } from '@bufbuild/protobuf/wire'


// -------- START PARSING PROTO DATA --------
// Auto-build reverse mapping from MessageFns to event key and field number
const MESSAGE_TYPE_TO_KEY = new Map<MessageFns<any>, string>()
const MESSAGE_TYPE_TO_FIELD_NUMBER = new Map<MessageFns<any>, number>()

// Build the mapping using references from metadata
const websocketMessageType = websocket_md.fileDescriptor.messageType?.find(
    msg => msg.name === 'WebsocketMessage'
)

if (websocketMessageType?.field) {
    for (const field of websocketMessageType.field) {
        // Look up the MessageFns in references using the typeName
        if (field.typeName) {
            const messageFns = websocket_md.references[field.typeName]
            if (messageFns && field.jsonName && field.number) {
                MESSAGE_TYPE_TO_KEY.set(messageFns, field.jsonName)
                MESSAGE_TYPE_TO_FIELD_NUMBER.set(messageFns, field.number)
            }
        }
    }
}

function get_name_from_messagetype(event_type: MessageFns<any>): string {
    const event = MESSAGE_TYPE_TO_KEY.get(event_type)
    if (!event) {
        throw new Error("Event type not found in 'WebsocketMessage'. The MessageFns you passed doesn't correspond to any WebsocketMessage field.");
    }
    return event
}

// Get field number from MessageFns type
function get_field_number_from_messagetype(event_type: MessageFns<any>): number {
    const fieldNumber = MESSAGE_TYPE_TO_FIELD_NUMBER.get(event_type)
    if (fieldNumber === undefined) {
        throw new Error("Field number not found in 'WebsocketMessage'. The MessageFns you passed doesn't correspond to any WebsocketMessage field.");
    }
    return fieldNumber
}

// -------- END PARSING PROTO DATA --------

const socketEvents = ['open', 'close', 'error', 'message', 'unresponsive'] as const
type SocketEvent = (typeof socketEvents)[number]

type TaggedSocketMessage = [string, WebsocketMessage]




const decodeMessage = (data: ArrayBuffer): TaggedSocketMessage => {

    const decoded = WebsocketMessage.decode(new Uint8Array(data));
    const values = Object.entries(decoded).filter(([, value]) => value !== undefined) // Filter all values which are not undefined
    if (values.length != 1) {
        throw new Error("Message included either 0 or more than 1 data point")
    }
    const [event, value] = values[0]
    return [event, decoded]
}

const encodeMessage = (data: WebsocketMessage): Uint8Array<ArrayBuffer> => {
    const encoded = WebsocketMessage.encode(data).finish();
    return encoded;
}

function createWebSocket() {
    const listeners = new Map<string, Set<(data?: unknown) => void>>()
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

    function getListeners<MT>(event_type: MessageFns<MT>): Set<(data?: unknown) => void>  {
        const event = get_field_number_from_messagetype(event_type);

        const event_listeners = listeners.get(event);
        if (event_listeners == undefined) {
            return new Set()
        }
        return event_listeners;
    }

    function disconnect(reason: SocketEvent, event?: Event) {
        ws.close()
        set(false)
        clearTimeout(unresponsiveTimeoutId)
        clearTimeout(reconnectTimeoutId)
        listeners.get(reason)?.forEach(listener => listener(event))
        reconnectTimeoutId = setTimeout(connect, reconnectTimeoutTime)
    }

    function connect() {
        ws = new WebSocket(socketUrl)
        ws.binaryType = 'arraybuffer'
        ws.onopen = ev => {
            ping()
            set(true)
            clearTimeout(reconnectTimeoutId)
            listeners.get('open')?.forEach(listener => listener(ev))
            for (const event of listeners.keys()) {
                if (socketEvents.includes(event as SocketEvent)) continue
                subscribeToEvent(event)
            }
        }
        ws.onmessage = frame => {
            resetUnresponsiveCheck()
            const [event, message] = decodeMessage(frame.data)
            if (event) listeners.get(event)?.forEach(listener => listener(message))
        }
        ws.onerror = ev => disconnect('error', ev)
        ws.onclose = ev => disconnect('close', ev)
    }

    function unsubscribe(event: string, listener?: (data: unknown) => void) {
        const eventListeners = listeners.get(event)
        if (!eventListeners) return

        if (!eventListeners.size) {
            unsubscribeToEvent(event)
        }
        if (listener) {
            eventListeners?.delete(listener)
        } else {
            listeners.delete(event)
        }
    }

    function resetUnresponsiveCheck() {
        clearTimeout(unresponsiveTimeoutId)
        unresponsiveTimeoutId = setTimeout(() => disconnect('unresponsive'), reconnectTimeoutTime)
    }

    // T must extend a type of WebsocketMessages
    function sendEvent<T>(event: MessageFns<T>, data: T) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        const type = get_event_from_messagetype(event);
        const wsm = WebsocketMessage.create();
        (wsm as any)[type] = data
        send(wsm)
    }

    function unsubscribeToEvent(event: string) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        send([1, event])
    }

    function subscribeToEvent(event: string) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        send([0, event])
    }

    function send(data: WebsocketMessage) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        const encoded = encodeMessage(data);
        ws.send(encoded);
    }

    function ping() {
        const serialized = encodeMessage([4])
        if (!serialized) {
            console.error('Could not serialize message')
            return
        }
        ws.send(serialized)
    }

    return {
        subscribe,
        sendEvent,
        init,
        on: <MT, T>(event_type: MessageFns<MT>, listener: (data: T) => void): (() => void) => {
            const event = get_event_from_messagetype(event_type);

            let eventListeners = listeners.get(event)
            if (!eventListeners) {
                if (!socketEvents.includes(event as SocketEvent)) {
                    subscribeToEvent(event)
                }
                eventListeners = new Set()
                listeners.set(event, eventListeners)
            }
            eventListeners.add(listener as (data: unknown) => void)

            return () => {
                unsubscribe(event, listener as (data: unknown) => void)
            }
        },
        off: <MT, T>(event_type: MessageFns<MT>, listener: (data: T) => void) => {
            const event = get_event_from_messagetype(event_type);

            unsubscribe(event, listener as (data: unknown) => void)
        }
    }
}

export const socket = createWebSocket()
