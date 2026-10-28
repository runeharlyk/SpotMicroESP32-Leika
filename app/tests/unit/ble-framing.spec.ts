import { describe, it, expect } from 'vitest'
import { readFileSync } from 'fs'
import path from 'path'
import {
    BLE_MAX_MESSAGE,
    BleReassembler,
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

// What the app makes of the robot's notifications, in order: each message, or the point the stream broke.
const reassemble = (notifications: Uint8Array[]) => {
    const reassembler = new BleReassembler()
    return notifications
        .map(notification => reassembler.push(notification))
        .filter(result => result !== 'partial')
        .map(result => (result === 'out-of-step' ? result : toHex(result)))
}

describe('BLE framing against the shared vectors', () => {
    it('has vectors to check', () => {
        expect(vectors.length).toBeGreaterThanOrEqual(12)
        expect(vectors.filter(vector => vector.chunk_size !== null).length).toBeGreaterThanOrEqual(
            7
        )
    })

    // A vector with a chunk size is an encoder's output, which is how the robot notifies. The others
    // describe what the robot's receiver tolerates in the app's writes; the robot never notifies so.
    for (const vector of vectors) {
        if (vector.chunk_size === null) {
            it(`finds "${vector.name}", which the robot never notifies, out of step`, () => {
                expect(reassemble(vector.chunks.map(fromHex))).toContain('out-of-step')
            })
            continue
        }

        it(`reassembles "${vector.name}" as the robot notifies it`, () => {
            expect(reassemble(vector.chunks.map(fromHex))).toEqual(vector.frames)
        })

        it(`encodes "${vector.name}" to the vector's chunks`, () => {
            const chunks = encodeBleFrame(fromHex(vector.frames[0]), vector.chunk_size!)
            expect(chunks.map(toHex)).toEqual(vector.chunks)
        })
    }
})

describe('BLE framing', () => {
    it('takes a message from the robot larger than it would take from the app', () => {
        const message = Uint8Array.from({ length: 6000 }, (_, i) => i % 251)

        expect(reassemble(encodeBleFrame(message, 244))).toEqual([toHex(message)])
    })

    it('hands over a message only once, whichever notification completes it', () => {
        const reassembler = new BleReassembler()
        const [first, second] = encodeBleFrame(Uint8Array.of(1, 2, 3, 4), 3)

        expect(reassembler.push(first)).toBe('partial')
        expect(toHex(reassembler.push(second) as Uint8Array)).toBe('01020304')
        expect(toHex(reassembler.push(encodeBleFrame(Uint8Array.of(5))[0]) as Uint8Array)).toBe(
            '05'
        )
    })

    // A length prefix alone would take the next message as the rest of the cut one, and read on.
    it('finds a message whose later chunk was lost out of step at the next message', () => {
        const cut = encodeBleFrame(
            Uint8Array.from({ length: 300 }, (_, i) => i % 256),
            182
        )
        const next = encodeBleFrame(
            Uint8Array.from({ length: 200 }, (_, i) => 255 - (i % 256)),
            182
        )

        expect(reassemble([cut[0], ...next])).toEqual(['out-of-step'])
    })

    it('finds a length over the largest message out of step', () => {
        const reassembler = new BleReassembler(4)

        expect(reassembler.push(Uint8Array.of(5, 0, 1, 2, 3, 4, 5))).toBe('out-of-step')
    })

    it('refuses to encode an empty message or one the length cannot describe', () => {
        expect(() => encodeBleFrame(new Uint8Array(0))).toThrow(RangeError)
        expect(() => encodeBleFrame(new Uint8Array(BLE_MAX_MESSAGE + 1))).toThrow(RangeError)
    })
})
