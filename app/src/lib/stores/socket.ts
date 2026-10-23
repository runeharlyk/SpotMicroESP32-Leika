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
import { protoMetadata as apiProtoMetadata } from '$lib/platform_shared/api'
import { telemetry } from './telemetry'
import type { ITransport, TransportHandlers } from '$lib/transport/transport.interface'
import { createWebSocketTransport } from '$lib/transport/websocket-adapter'
import { BLE_RECONNECT_ATTEMPTS, createBleTransport } from '$lib/transport/ble-adapter'
import { createSerialTransport } from '$lib/transport/serial-adapter'
import { LatestFrames } from '$lib/transport/latest-frames'
import { SETTING_GRACE_MS, kindOfRequest } from '$lib/transport/request-kinds'

// The messages only the newest of which counts: a stick frame waiting behind a slow write is replaced by the next.
const MOTION_FRAMES: ReadonlySet<MessageFns<unknown>> = new Set([Messages.ControllerData])

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
    ...filesystemProtoMetadata.references,
    ...apiProtoMetadata.references
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

/**
 * `robot`: the user chose a robot, which may be another than the one before, so what the app knows
 * of the last one is to be forgotten. It comes before the chosen robot's link opens. A link that
 * drops and comes back reaches the same robot, and says nothing.
 */
type SocketEvent = 'open' | 'close' | 'error' | 'message' | 'unresponsive' | 'robot'

type TaggedMessage = { tag: number; msg: Message }

/** Undefined for a message kind this app does not know, as a newer firmware may send. */
export const decodeMessage = (data: ArrayBuffer): TaggedMessage | undefined => {
    const decoded = Message.decode(new Uint8Array(data))
    const values = Object.entries(decoded).filter(([, value]) => value !== undefined)
    if (values.length === 0) return
    if (values.length > 1) {
        throw new Error('Message included more than 1 data point')
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

/** What a request that cannot reach the robot fails with. */
export const NOT_CONNECTED = 'The robot is not connected'

export function createWebSocket({
    requestTimeoutTime = 30000,
    settingGraceTime = SETTING_GRACE_MS
} = {}) {
    // Listeners also get the frame as it arrived, for a recorder that keeps the robot's bytes.
    const message_listeners = new Map<number, Set<(data: unknown, frame: ArrayBuffer) => void>>()
    const event_listeners = new Map<string, Set<(data?: unknown) => void>>()
    const pending_requests = new Map<number, PendingRequest>()
    const queued_requests = new Map<
        string,
        {
            data: CorrelationRequestData
            resolve: (r: CorrelationResponse) => void
            reject: (e: Error) => void
            timeoutId: ReturnType<typeof setTimeout>
            deadline: number
        }
    >()
    const { subscribe, set } = writable(false)
    const reconnectBaseDelay = 1000
    const reconnectMaxDelay = 10000
    const pingIntervalTime = 4000
    const unresponsiveTimeoutTime = 12000
    let reconnectAttempts = 0
    let lastPingSentAt = 0
    let correlationIdCounter = 0
    let unresponsiveTimeoutId: ReturnType<typeof setTimeout>
    let reconnectTimeoutId: ReturnType<typeof setTimeout>
    let pingIntervalId: ReturnType<typeof setInterval>
    let transport: ITransport | undefined
    let socketUrl: string | URL
    // The port the user granted; a serial link that is lost, as in a reset, reopens it.
    let serialPort: SerialPort | undefined
    // The device the user chose; a Bluetooth link that drops reconnects it, without the chooser.
    let bluetoothDevice: BluetoothDevice | undefined
    // A device or port the user chose, from a chooser or another than the one in use: the robot on
    // it is told as chosen when its link opens, since a chooser names it only then.
    let robotChosen = false
    const activeTransport = writable<ITransport['kind'] | null>(null)

    const isOpen = () => transport?.isConnected() === true
    const motionFrames = new LatestFrames({
        busy: () => transport?.busy() ?? false,
        write: data => transport?.send(data)
    })

    function getRequestKey(data: CorrelationRequestData): string {
        return (
            Object.keys(data).find(k => data[k as keyof CorrelationRequestData] !== undefined) ??
            'unknown'
        )
    }

    function init(url: string | URL) {
        robotChosen = false
        if (socketUrl !== undefined && String(url) !== String(socketUrl)) chooseRobot()
        socketUrl = url
        connect()
    }

    /** Drops the active transport without reporting it; its late events are ignored from here on. */
    function detach() {
        const previous = transport
        transport = undefined
        previous?.close()
        set(false)
        activeTransport.set(null)
        motionFrames.clear()
        clearTimeout(unresponsiveTimeoutId)
        clearTimeout(reconnectTimeoutId)
        clearInterval(pingIntervalId)
        // What was sent on the old link can never be answered: its callers learn so now, not at the timeout.
        for (const [correlationId, pending] of pending_requests) {
            clearTimeout(pending.timeoutId)
            pending.reject(
                new Error(`The connection closed before the reply (id: ${correlationId})`)
            )
        }
        pending_requests.clear()
        return previous
    }

    /** Replaces the active transport, so a robot being switched away from cannot feed or close its successor. */
    function attach<T extends ITransport>(create: (handlers: TransportHandlers) => T): T {
        detach()
        const next = create({
            onOpen: () => {
                if (transport === next) handleOpen()
            },
            onData: data => {
                if (transport === next) handleData(data)
            },
            onClose: (reason, event) => {
                if (transport === next) disconnect(reason, event)
            },
            onDrained: () => {
                if (transport === next) motionFrames.drained()
            }
        })
        transport = next
        return next
    }

    /**
     * Web Bluetooth needs a user gesture, so this is called from a click rather than on mount.
     * BLE carries the same protobuf envelope as the WebSocket, length-prefixed and chunked to the
     * ATT payload; the robot streams no telemetry over it and moves no files or firmware.
     */
    async function connectBluetooth() {
        const ble = attach(createBleTransport)
        robotChosen = true
        try {
            await ble.connect()
            bluetoothDevice = ble.device
        } catch (error) {
            robotChosen = false
            // Pairing was cancelled or failed. Fall back to WiFi, or a dead BLE transport would be
            // left in place, and nothing would redial.
            if (socketUrl) connect()
            throw error
        }
    }

    /**
     * Web Serial asks for a port with a user gesture, so this is called from a click unless `port`
     * was granted before. On the robot's native USB port the link carries all the WebSocket does;
     * through a USB-serial chip, on the console UART, it carries requests only.
     */
    async function connectSerial(port?: SerialPort) {
        const serial = attach(handlers => createSerialTransport(handlers, port))
        robotChosen = port === undefined || port !== serialPort
        try {
            await serial.connect()
            serialPort = serial.port
        } catch (error) {
            robotChosen = false
            // The port chooser was cancelled or the port would not open; as for BLE, fall back to WiFi.
            if (socketUrl) connect()
            throw error
        }
    }

    /** Waits for the granted port to come back, as a native USB board's does after a reset, and opens it. */
    function reopenSerial() {
        const serial = attach(handlers => createSerialTransport(handlers, serialPort))
        serial.connect().then(
            () => (serialPort = serial.port),
            error => {
                if (transport === serial) disconnect('error', error)
            }
        )
    }

    /** Reconnects the device the user chose, which needs no user gesture. */
    function reconnectBluetooth() {
        const ble = attach(handlers => createBleTransport(handlers, bluetoothDevice))
        ble.connect().catch(error => {
            if (transport === ble) disconnect('error', error)
        })
    }

    const redial = (kind: ITransport['kind'] | undefined) =>
        kind === 'serial' ? reopenSerial
        : kind === 'bluetooth' ? reconnectBluetooth
        : connect

    function disconnect(reason: SocketEvent, event?: unknown) {
        const closed = detach()
        event_listeners.get(reason)?.forEach(listener => listener(event))
        // Choosing a Bluetooth device or a serial port needs a user gesture, so a link that never had one stays down.
        if (closed && !closed.canAutoReconnect) return
        // A robot out of range or switched off is not tried for ever; the user connects it again.
        if (closed?.kind === 'bluetooth' && reconnectAttempts >= BLE_RECONNECT_ATTEMPTS) {
            console.warn(`Bluetooth reconnect gave up after ${BLE_RECONNECT_ATTEMPTS} attempts`)
            reconnectAttempts = 0
            return
        }
        const delay = Math.min(reconnectBaseDelay * 2 ** reconnectAttempts, reconnectMaxDelay)
        reconnectAttempts++
        reconnectTimeoutId = setTimeout(redial(closed?.kind), delay)
    }

    function chooseRobot() {
        event_listeners.get('robot')?.forEach(listener => listener(undefined))
    }

    function handleOpen() {
        if (robotChosen) {
            robotChosen = false
            chooseRobot()
        }
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
    }

    function handleData(data: ArrayBuffer) {
        resetUnresponsiveCheck()

        const decoded = decodeMessage(data)
        if (!decoded) return
        const { tag, msg } = decoded
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
                ?.forEach(listener => listener(msg[key as keyof typeof msg], data))
        }
    }

    function connect() {
        attach(handlers => createWebSocketTransport(socketUrl, handlers)).connect()
    }

    function unsubscribe<MT>(
        event_type: MessageFns<MT>,
        listener: (data: MT, frame: ArrayBuffer) => void
    ) {
        const tag = getTagFromMessageType(event_type)
        const message_listeners_totag = message_listeners.get(tag)
        if (!message_listeners_totag) return

        message_listeners_totag.delete(listener as (data: unknown, frame: ArrayBuffer) => void)
        if (message_listeners_totag.size == 0) {
            // Dropped, so the next listener for this tag subscribes again.
            message_listeners.delete(tag)
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
        if (MOTION_FRAMES.has(event as MessageFns<unknown>))
            motionFrames.offer(getTagFromMessageType(event), encodeMessage(wsm as Message))
        else send(wsm as Message)
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
        reject: (e: Error) => void,
        timeout = requestTimeoutTime
    ) {
        const correlationId = ++correlationIdCounter
        const timeoutId = setTimeout(() => {
            pending_requests.delete(correlationId)
            reject(new Error(`Request timeout (id: ${correlationId})`))
        }, timeout)

        pending_requests.set(correlationId, { resolve, reject, timeoutId })

        const request = CorrelationRequest.create({ correlationId, ...data })
        send(Message.create({ correlationRequest: request }))
    }

    // A request held for a link keeps the timeout it got when it was made.
    function flushQueuedRequests() {
        for (const [, { data, resolve, reject, timeoutId, deadline }] of queued_requests) {
            clearTimeout(timeoutId)
            request(data, resolve, reject, deadline - Date.now())
        }
        queued_requests.clear()
    }

    return {
        subscribe,
        emit,
        init,
        connectBluetooth,
        connectSerial,
        transport: { subscribe: activeTransport.subscribe },
        on: <MT>(
            event_type: MessageFns<MT>,
            listener: (data: MT, frame: ArrayBuffer) => void
        ): (() => void) => {
            const tag = getTagFromMessageType(event_type)

            let message_listeners_totag = message_listeners.get(tag)
            if (!message_listeners_totag) {
                message_listeners_totag = new Set()
                message_listeners.set(tag, message_listeners_totag)
                subscribeToEvent(event_type)
            }
            message_listeners_totag.add(listener as (data: unknown, frame: ArrayBuffer) => void)

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
            return new Promise((ownResolve, ownReject) => {
                if (isOpen()) {
                    request(data, ownResolve, ownReject)
                } else {
                    // While no link is up, the request's kind decides (transport/request-kinds.ts).
                    const kind = kindOfRequest(data)
                    if (kind === 'action') return ownReject(new Error(NOT_CONNECTED))
                    const key = getRequestKey(data)
                    let resolve: (response: CorrelationResponse) => void = ownResolve
                    let reject: (error: Error) => void = ownReject
                    // Only the newest of the same request is sent; an older caller waiting for the
                    // same answer settles with it.
                    const existing = queued_requests.get(key)
                    if (existing) {
                        clearTimeout(existing.timeoutId)
                        resolve = response => {
                            existing.resolve(response)
                            ownResolve(response)
                        }
                        reject = error => {
                            existing.reject(error)
                            ownReject(error)
                        }
                    }
                    // A queued request must expire too, or a request issued while disconnected
                    // never settles and its caller waits forever. A settings write waits out a
                    // link blip only, and then fails as not saved.
                    const setting = kind === 'setting'
                    const timeoutId = setTimeout(
                        () => {
                            queued_requests.delete(key)
                            reject(
                                new Error(
                                    setting ? NOT_CONNECTED : (
                                        `Request timeout while disconnected (${key})`
                                    )
                                )
                            )
                        },
                        setting ? settingGraceTime : requestTimeoutTime
                    )
                    const deadline = Date.now() + requestTimeoutTime
                    queued_requests.set(key, { data, resolve, reject, timeoutId, deadline })
                }
            })
        }
    }
}

export const socket = createWebSocket()
