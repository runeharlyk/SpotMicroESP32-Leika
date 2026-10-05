import { writable } from 'svelte/store'
import { Message } from '$lib/platform_shared/message'
import { serialLog } from '$lib/stores/serial-log'
import type { ITransport, TransportHandlers } from './transport.interface'
import { SERIAL_MAX_MESSAGE, SerialFrameDecoder, encodeSerialFrame } from './serial-framing'

/** The firmware's console speed, which the protocol shares with its log. */
export const SERIAL_BAUD_RATE = 115200

/** How often a robot that has not answered yet is pinged; it may still be booting. */
export const SERIAL_PROBE_INTERVAL = 1000

const PING_FRAME = encodeSerialFrame(Message.encode(Message.create({ pingmsg: {} })).finish())

/** Web Serial exists in Chrome and Edge, on pages served over https or from localhost. */
export const isSerialSupported = () => typeof navigator !== 'undefined' && 'serial' in navigator

/** Whether a serial port is open, whether or not the robot on it has answered yet. */
export const serialPortOpen = writable(false)

/** Ports a previous transport is still closing; opening one before then fails. */
const closingPorts = new WeakMap<SerialPort, Promise<void>>()

export type SerialTransport = ITransport & { readonly port: SerialPort | undefined }

const sameDevice = (a: SerialPort, b: SerialPort) => {
    const x = a.getInfo()
    const y = b.getInfo()
    return (
        x.usbVendorId !== undefined &&
        x.usbVendorId === y.usbVendorId &&
        x.usbProductId === y.usbProductId
    )
}

/**
 * Resolves to `granted` once it is present, or to the same device under a new port: a native USB
 * board leaves while it resets and may come back as another port. Needs no user gesture.
 */
const presentPort = (granted: SerialPort, signal: AbortSignal) =>
    new Promise<SerialPort>((resolve, reject) => {
        const matches = (port: SerialPort) => port === granted || sameDevice(port, granted)
        const settle = (outcome: () => void) => {
            navigator.serial.removeEventListener('connect', onConnect)
            signal.removeEventListener('abort', onAbort)
            outcome()
        }
        const onConnect = (event: Event) => {
            const port = event.target as SerialPort
            if (matches(port)) settle(() => resolve(port))
        }
        const onAbort = () => settle(() => reject(signal.reason))
        // Listening before asking, so a port arriving in between is not missed.
        navigator.serial.addEventListener('connect', onConnect)
        signal.addEventListener('abort', onAbort)
        navigator.serial.getPorts().then(ports => {
            const port = ports.find(p => p === granted) ?? ports.find(matches)
            if (port) settle(() => resolve(port))
        }, reject)
    })

/**
 * The robot's protobuf messages over USB serial, framed so they share the port with its log.
 * `connect` opens `granted`, or asks the user for a port when none is given, which needs a user
 * gesture. The link counts as open once the robot answers: a board resets when its port opens and
 * stays silent while it boots, and whatever is sent meanwhile is lost.
 */
export const createSerialTransport = (
    handlers: TransportHandlers,
    granted?: SerialPort
): SerialTransport => {
    let port = granted
    let opened: SerialPort | undefined
    let answering = false
    let closed = false
    let reader: ReadableStreamDefaultReader<Uint8Array> | undefined
    let reading = Promise.resolve()
    let writeQueue = Promise.resolve()
    let probeId: ReturnType<typeof setInterval> | undefined
    const abort = new AbortController()

    const decoder = new SerialFrameDecoder({
        onFrame: message => {
            // The first frame only answers a probe, as nothing else is sent before it.
            if (!answering) {
                answering = true
                clearInterval(probeId)
                handlers.onOpen()
                return
            }
            try {
                handlers.onData(message.buffer as ArrayBuffer)
            } catch (error) {
                // Thrown on, it would end the read and lose the rest of the chunk.
                console.error('A serial frame could not be handled:', error)
            }
        },
        onLogLine: line => serialLog.append(line)
    })

    const write = (frame: Uint8Array) => {
        const writable = port?.writable
        if (!writable) return
        writeQueue = writeQueue
            .then(async () => {
                const writer = writable.getWriter()
                try {
                    await writer.write(frame)
                } finally {
                    writer.releaseLock()
                }
            })
            .catch(error => console.error('Serial write failed:', error))
    }

    const stop = () => {
        clearInterval(probeId)
        answering = false
        serialPortOpen.set(false)
    }

    const readUntilLost = async (open: SerialPort) => {
        while (open.readable && !closed) {
            reader = open.readable.getReader()
            try {
                for (let chunk = await reader.read(); !chunk.done; chunk = await reader.read()) {
                    decoder.feed(chunk.value)
                }
                break
            } catch {
                // A parity or framing error hands over a fresh port.readable; a lost device leaves none.
            } finally {
                reader.releaseLock()
            }
        }
        if (closed) return
        stop()
        opened = undefined
        await open.close().catch(() => {})
        handlers.onClose('close')
    }

    return {
        kind: 'serial',
        get canAutoReconnect() {
            return port !== undefined
        },
        maxFrameSize: SERIAL_MAX_MESSAGE,

        get port() {
            return port
        },

        connect: async () => {
            if (!isSerialSupported()) {
                throw new Error('Web Serial is unavailable. It needs Chrome or Edge over HTTPS.')
            }
            const chosen =
                port ? await presentPort(port, abort.signal) : await navigator.serial.requestPort()
            port = chosen
            await closingPorts.get(chosen)
            await chosen.open({ baudRate: SERIAL_BAUD_RATE })
            if (closed) {
                await chosen.close()
                throw new Error('The serial link was closed while its port opened')
            }
            opened = chosen
            serialPortOpen.set(true)
            // Boards with an auto-reset circuit are held in reset or in the bootloader while
            // these are asserted. Not every port can set them, and that is no reason to fail.
            await chosen
                .setSignals({ dataTerminalReady: false, requestToSend: false })
                .catch(() => {})
            reading = readUntilLost(chosen)
            write(PING_FRAME)
            probeId = setInterval(() => write(PING_FRAME), SERIAL_PROBE_INTERVAL)
        },

        close: () => {
            if (closed) return
            closed = true
            abort.abort()
            stop()
            const open = opened
            if (!open) return
            void reader?.cancel().catch(() => {})
            const closing = reading
                .then(() => writeQueue)
                .then(() => open.close())
                .catch(error => console.error('Closing the serial port failed:', error))
                .finally(() => closingPorts.delete(open))
            closingPorts.set(open, closing)
        },

        isConnected: () => answering && !closed,

        send: data => {
            if (!answering || closed) return
            if (data.length > SERIAL_MAX_MESSAGE) {
                console.warn(
                    `Dropping ${data.length} byte message: serial frames are capped at ${SERIAL_MAX_MESSAGE} bytes.`
                )
                return
            }
            write(encodeSerialFrame(data))
        }
    }
}
