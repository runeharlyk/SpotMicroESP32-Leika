import { writable } from 'svelte/store'
import { encode, decode } from '@msgpack/msgpack'
import { WebsocketMessage, type MessageFns } from '$lib/platform_shared/websocket_message'
import * as WebsocketMessages from '$lib/platform_shared/websocket_message'
import type { BinaryWriter } from '@bufbuild/protobuf/wire'

// Auto-build reverse mapping from MessageFns to event key
const MESSAGE_TYPE_TO_KEY = new Map<MessageFns<any>, string>()

// Iterate through all exports and match them to WebsocketMessage fields
for (const [exportName, exportValue] of Object.entries(WebsocketMessages)) {
    // Check if this export is a MessageFns (has encode/decode methods)
    if (exportValue && typeof exportValue === 'object' && 'encode' in exportValue && 'decode' in exportValue) {
        // Try to find matching key in WebsocketMessage by creating an instance and checking
        const messageKeys = Object.keys(WebsocketMessage.create()) as Array<keyof typeof WebsocketMessage>

        for (const key of messageKeys) {
            // Match by naming convention: exportName should match key in some pattern
            // Common patterns: "IMUData" -> "imu", "RSSIData" -> "rssi"
            const keyLower = key.toLowerCase()
            const exportLower = exportName.toLowerCase().replace(/data$/, '')

            if (keyLower === exportLower) {
                MESSAGE_TYPE_TO_KEY.set(exportValue as MessageFns<any>, key)
                break
            }
        }
    }
}

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
        const event = MESSAGE_TYPE_TO_KEY.get(event_type)
        if (!event) {
            throw new Error("Event type not found in 'WebsocketMessage'. The MessageFns you passed doesn't correspond to any WebsocketMessage field.");
        }

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

    function sendEvent(event: string, data: unknown) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return
        send([2, event, data])
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
            const event = MESSAGE_TYPE_TO_KEY.get(event_type)

            if (!event) {
                throw new Error("Event type not found in 'WebsocketMessage'. The MessageFns you passed doesn't correspond to any WebsocketMessage field.");
            }

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
            const event = MESSAGE_TYPE_TO_KEY.get(event_type)
            if (!event) {
                throw new Error("Event type not found in 'WebsocketMessage'. The MessageFns you passed doesn't correspond to any WebsocketMessage field.");
            }

            unsubscribe(event, listener as (data: unknown) => void)
        }
    }
}

export const socket = createWebSocket()
