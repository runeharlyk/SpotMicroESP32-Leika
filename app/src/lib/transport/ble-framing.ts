/**
 * Protobuf messages over the Nordic UART Service, whose writes and notifications each hold at most
 * one ATT payload. A message goes out as a 2-byte little-endian length and the message, split into
 * chunks of the payload size; every message starts a new chunk. The firmware's ble_frame.h
 * implements the same, and platform_shared/ble_frame_vectors.json pins both. The app reads the
 * robot's notifications more strictly than the robot reads the app's writes (BleReassembler).
 */

const HEADER = 2

/** The largest message the robot takes, the size of its decode buffer. */
export const BLE_MAX_OUTGOING = 4096
/** The largest message the length prefix can describe; the robot may send up to this. */
export const BLE_MAX_MESSAGE = 0xffff

/**
 * The chunk size the app writes in. Web Bluetooth does not tell the negotiated MTU, and a write
 * without response larger than its payload fails; 180 fits an ATT MTU of 183 or more, and is
 * what Hexapod's app writes.
 */
export const BLE_WRITE_CHUNK = 180

/** The writes for one message, in order. */
export function encodeBleFrame(
    message: Uint8Array,
    chunkSize = BLE_WRITE_CHUNK
): Uint8Array<ArrayBuffer>[] {
    if (message.length === 0 || message.length > BLE_MAX_MESSAGE) {
        throw new RangeError(
            `A BLE frame carries 1 to ${BLE_MAX_MESSAGE} bytes, not ${message.length}`
        )
    }
    const framed = new Uint8Array(HEADER + message.length)
    framed[0] = message.length & 0xff
    framed[1] = message.length >> 8
    framed.set(message, HEADER)
    const chunks: Uint8Array<ArrayBuffer>[] = []
    for (let at = 0; at < framed.length; at += chunkSize) {
        chunks.push(framed.slice(at, at + chunkSize))
    }
    return chunks
}

/** A whole message, `'partial'` while one is still arriving, `'out-of-step'` once the stream broke. */
export type Reassembled = Uint8Array<ArrayBuffer> | 'partial' | 'out-of-step'

/**
 * Rebuilds the robot's messages from the notifications of one connection, fed as they arrive. The
 * robot starts every message in a new notification and sends it in chunks of one size and a shorter
 * last one (esp32/include/communication/ble_link.h), so a notification never holds bytes of two
 * messages, and a message's first notification is a full chunk unless it holds the whole message.
 * A length prefix alone cannot find its way back once a message was cut short: it would complete
 * the cut message with the next ones' bytes and then read lengths from the middle of payloads.
 * These rules show a stream out of step at the first notification that breaks them. The robot ends
 * the connection rather than send a message cut short, so this guards against a notification lost
 * on the way.
 */
export class BleReassembler {
    private message: Uint8Array<ArrayBuffer> | null = null
    private filled = 0
    private chunk = 0

    constructor(private readonly maxMessage = BLE_MAX_MESSAGE) {}

    push(notification: Uint8Array): Reassembled {
        if (!this.message) return this.start(notification)
        const remaining = this.message.length - this.filled
        if (notification.length !== Math.min(this.chunk, remaining)) return this.outOfStep()
        this.message.set(notification, this.filled)
        this.filled += notification.length
        if (this.filled < this.message.length) return 'partial'
        const whole = this.message.slice(HEADER)
        this.reset()
        return whole
    }

    private reset() {
        this.message = null
        this.filled = 0
        this.chunk = 0
    }

    private start(notification: Uint8Array): Reassembled {
        if (notification.length < HEADER) return this.outOfStep()
        const length = notification[0] | (notification[1] << 8)
        if (length === 0 || length > this.maxMessage) return this.outOfStep()
        const total = HEADER + length
        if (notification.length > total) return this.outOfStep()
        if (notification.length === total) return notification.slice(HEADER)
        this.message = new Uint8Array(total)
        this.message.set(notification)
        this.filled = notification.length
        this.chunk = notification.length
        return 'partial'
    }

    private outOfStep(): Reassembled {
        this.reset()
        return 'out-of-step'
    }
}
