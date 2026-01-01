import { writable } from 'svelte/store'
import { encode, decode } from '@msgpack/msgpack'
import { WebsocketMessage, type MessageFns, protoMetadata as websocket_md } from '$lib/platform_shared/websocket_message'
import * as WebsocketMessages from '$lib/platform_shared/websocket_message'
import type { BinaryWriter } from '@bufbuild/protobuf/wire'


// -------- START PARSING PROTO DATA --------
// Auto-build reverse mapping from MessageFns to event key and tag
export const MESSAGE_TYPE_TO_KEY = new Map<MessageFns<any>, string>()
export const MESSAGE_TYPE_TO_TAG = new Map<MessageFns<any>, number>()
export const MESSAGE_KEY_TO_TAG = new Map<string, number>()

// Build the mapping using references from metadata
const websocketMessageType = websocket_md.fileDescriptor.messageType?.find(
    (    msg: { name: string } ) => msg.name === 'WebsocketMessage'
)

if (websocketMessageType?.field) {
    for (const field of websocketMessageType.field) {
        // Look up the MessageFns in references using the typeName
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

function get_name_from_messagetype(event_type: MessageFns<any>): string {
    const event = MESSAGE_TYPE_TO_KEY.get(event_type)
    if (!event) {
        throw new Error("Event type not found in 'WebsocketMessage'. The MessageFns you passed doesn't correspond to any WebsocketMessage field.");
    }
    return event
}

// Get tag from MessageFns type
function get_tag_from_messagetype(event_type: MessageFns<any>): number {
    const fieldNumber = MESSAGE_TYPE_TO_TAG.get(event_type)
    if (fieldNumber === undefined) {
        throw new Error("Tag not found in 'WebsocketMessage'. The MessageFns you passed doesn't correspond to any WebsocketMessage field.");
    }
    return fieldNumber
}

// -------- END PARSING PROTO DATA --------

const socketEvents = ['open', 'close', 'error', 'message', 'unresponsive'] as const
type SocketEvent = (typeof socketEvents)[number]

type TaggedSocketMessage = {"tag": number, "msg": WebsocketMessage}



// Only exported for socket test
export const decodeMessage = (data: ArrayBuffer): TaggedSocketMessage => {

    const decoded = WebsocketMessage.decode(new Uint8Array(data));
    const values = Object.entries(decoded).filter(([, value]) => value !== undefined) // Filter all values which are not undefined
    if (values.length != 1) {
        throw new Error("Message included either 0 or more than 1 data point")
    }
    const fieldName = values[0][0]
    const tag = MESSAGE_KEY_TO_TAG.get(fieldName)
    if (tag === undefined) {
        throw new Error(`Tag not found for field: ${fieldName}`)
    }
    return {"tag": tag, "msg": decoded}
}

export const encodeMessage = (data: WebsocketMessage): Uint8Array<ArrayBuffer> => {
    const encoded = WebsocketMessage.encode(data).finish();
    return encoded;
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

    function getMsgListeners<MT>(event_type: MessageFns<MT>): Set<(data?: unknown) => void>  {
        const type_tag = get_tag_from_messagetype(event_type)

        const type_listeners = message_listeners.get(type_tag);
        if (type_listeners == undefined) {
            return new Set()
        }
        return type_listeners;
    }
    function getListeners<MT>(event: string): Set<(data?: unknown) => void>  {

        const event_listeners_forevent = event_listeners.get(event);
        if (event_listeners_forevent == undefined) {
            return new Set()
        }
        return event_listeners_forevent;
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
            event_listeners.get('open')?.forEach(listener => listener(ev))
            // TODO: Check if this makes sense? we also call subscribe to event when a new listen calls the "on" function
            // for (const event of listeners.keys()) {
            //     if (socketEvents.includes(event as SocketEvent)) continue
            //     subscribeToEvent(event)
            // }
        }
        ws.onmessage = frame => {
            resetUnresponsiveCheck()
            const {tag, msg} = decodeMessage(frame.data)
            if (tag) message_listeners.get(tag)?.forEach(listener => listener(msg))
        }
        ws.onerror = ev => disconnect('error', ev)
        ws.onclose = ev => disconnect('close', ev)
    }

    function unsubscribe(event_type: MessageFns<any>, listener: (data: unknown) => void) {
        const tag = get_tag_from_messagetype(event_type)
        const message_listeners_totag = message_listeners.get(tag)
        if (!message_listeners_totag) return

        // TODO: This looks like it deletes an individual listener, but unsubscribe unsubscribes for everyone. Not sure what it is supposed to do right now
        message_listeners_totag?.delete(listener)
        if (message_listeners_totag.size == 0) { // No more listeners, so we can unsubscribe
            unsubscribeToEvent(event_type)
        }
        
    }

    function resetUnresponsiveCheck() {
        clearTimeout(unresponsiveTimeoutId)
        unresponsiveTimeoutId = setTimeout(() => disconnect('unresponsive'), reconnectTimeoutTime)
    }

    // T must extend a type of WebsocketMessages
    function sendEvent<T>(event: MessageFns<T>, data: T) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        const type = get_name_from_messagetype(event);
        const wsm = WebsocketMessage.create();
        (wsm as any)[type] = data
        send(wsm)
    }

    function unsubscribeToEvent<T>(event_type: MessageFns<T>) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        const event = get_name_from_messagetype(event_type);
        const unsub_msg = WebsocketMessages.UnsubscribeNotification.create(
            {tag: get_tag_from_messagetype(event_type)}
        );
        send(WebsocketMessage.create({unsubNotif: unsub_msg}));
    }

    function subscribeToEvent<T>(event_type: MessageFns<T>) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        const event = get_name_from_messagetype(event_type);
        const sub_msg = WebsocketMessages.SubscribeNotification.create(
            {tag: get_tag_from_messagetype(event_type)}
        );
        send(WebsocketMessage.create({subNotif: sub_msg}));
    }

    function send(data: WebsocketMessage) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        const encoded = encodeMessage(data);
        ws.send(encoded);
    }

    function ping() {
        send(WebsocketMessage.create({pingmsg: {}}))
    }

    return {
        subscribe,
        sendEvent,
        init,
        on: <MT, T>(event_type: MessageFns<MT>, listener: (data: T) => void): (() => void) => {
            const tag = get_tag_from_messagetype(event_type);

            let message_listeners_totag = message_listeners.get(tag)
            if (!message_listeners_totag) {
                // If this is the first listener to this event, also call subscribe to the server
                message_listeners_totag = new Set()
                message_listeners.set(tag, message_listeners_totag)
            }
            message_listeners_totag.add(listener as (data: unknown) => void)

            return () => {
                unsubscribe(event_type, listener as (data: unknown) => void)
            }
        },
        off: <MT, T>(event_type: MessageFns<MT>, listener: (data: T) => void) => {
            unsubscribe(event_type, listener as (data: unknown) => void)
        }
    }
}

export const socket = createWebSocket()
