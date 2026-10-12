/**
 * Protobuf messages over the Nordic UART Service, whose writes and notifications each hold at most
 * one ATT payload. A message goes out as a 2-byte little-endian length and the message, split into
 * chunks of the payload size; every message starts a new chunk. The firmware's ble_frame.h
 * implements the same, and platform_shared/ble_frame_vectors.json pins both.
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

/**
 * Rebuilds messages from the notifications of one connection, fed as they arrive. A length of zero
 * or over the limit can only come from a lost chunk, so the buffered bytes are dropped; as every
 * message starts a new chunk, the next one starts afresh.
 */
export class BleFrameDecoder {
    private buffer = new Uint8Array(0)

    constructor(
        private readonly onFrame: (message: Uint8Array) => void,
        private readonly maxMessage = BLE_MAX_MESSAGE
    ) {}

    feed(chunk: Uint8Array) {
        const merged = new Uint8Array(this.buffer.length + chunk.length)
        merged.set(this.buffer)
        merged.set(chunk, this.buffer.length)
        let at = 0
        while (merged.length - at >= HEADER) {
            const length = merged[at] | (merged[at + 1] << 8)
            if (length === 0 || length > this.maxMessage) {
                this.buffer = new Uint8Array(0)
                return
            }
            if (merged.length - at < HEADER + length) break
            this.onFrame(merged.slice(at + HEADER, at + HEADER + length))
            at += HEADER + length
        }
        this.buffer = merged.slice(at)
    }
}
