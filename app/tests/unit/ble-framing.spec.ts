import { describe, it, expect } from 'vitest'
import { readFileSync } from 'fs'
import path from 'path'
import {
    BLE_MAX_MESSAGE,
    BLE_MAX_OUTGOING,
    BleFrameDecoder,
    encodeBleFrame
} from '../../src/lib/transport/ble-framing'

// The firmware's host tests read the same file, so both sides chunk and rebuild messages alike.
type Vector = { name: string; chunk_size: number | null; chunks: string[]; frames: string[] }
const { vectors } = JSON.parse(
    readFileSync(
        path.resolve(__dirname, '../../../platform_shared/ble_frame_vectors.json'),
        'utf-8'
    )
) as { vectors: Vector[] }

const fromHex = (hex: string) => Uint8Array.from(hex.match(/../g) ?? [], byte => parseInt(byte, 16))
const toHex = (bytes: Uint8Array) => Buffer.from(bytes).toString('hex')

// The vectors describe the robot's receiver, which takes messages of up to BLE_MAX_OUTGOING bytes.
const decode = (chunks: Uint8Array[], maxMessage = BLE_MAX_OUTGOING) => {
    const frames: string[] = []
    const decoder = new BleFrameDecoder(message => frames.push(toHex(message)), maxMessage)
    chunks.forEach(chunk => decoder.feed(chunk))
    return frames
}

describe('BLE framing against the shared vectors', () => {
    it('has vectors to check', () => {
        expect(vectors.length).toBeGreaterThanOrEqual(12)
        expect(vectors.filter(vector => vector.chunk_size !== null).length).toBeGreaterThanOrEqual(
            7
        )
    })

    for (const vector of vectors) {
        it(`decodes "${vector.name}"`, () => {
            expect(decode(vector.chunks.map(fromHex))).toEqual(vector.frames)
        })

        if (vector.chunk_size !== null) {
            it(`encodes "${vector.name}" to the vector's chunks`, () => {
                const chunks = encodeBleFrame(fromHex(vector.frames[0]), vector.chunk_size!)
                expect(chunks.map(toHex)).toEqual(vector.chunks)
            })
        }
    }
})

describe('BLE framing', () => {
    it('takes a message from the robot larger than it would take from the app', () => {
        const message = Uint8Array.from({ length: 6000 }, (_, i) => i % 251)

        expect(decode(encodeBleFrame(message, 244), BLE_MAX_MESSAGE)).toEqual([toHex(message)])
    })

    it('hands over a message only once, whichever notification completes it', () => {
        const delivered: Uint8Array[] = []
        const decoder = new BleFrameDecoder(message => delivered.push(message))
        const [first, second] = encodeBleFrame(Uint8Array.of(1, 2, 3, 4), 3)

        decoder.feed(first)
        expect(delivered).toHaveLength(0)
        decoder.feed(second)
        decoder.feed(encodeBleFrame(Uint8Array.of(5))[0])

        expect(delivered.map(toHex)).toEqual(['01020304', '05'])
    })

    it('refuses to encode an empty message or one the length cannot describe', () => {
        expect(() => encodeBleFrame(new Uint8Array(0))).toThrow(RangeError)
        expect(() => encodeBleFrame(new Uint8Array(BLE_MAX_MESSAGE + 1))).toThrow(RangeError)
    })
})
