import { describe, it, expect } from 'vitest'
import { readFileSync } from 'fs'
import path from 'path'
import {
    SERIAL_MAX_MESSAGE,
    SerialFrameDecoder,
    encodeSerialFrame
} from '../../src/lib/transport/serial-framing'

// The firmware's host tests read the same file, so both sides split a stream identically.
type Vector = { name: string; stream: string; frames: string[]; lines: string[] | null }
const { vectors } = JSON.parse(
    readFileSync(
        path.resolve(__dirname, '../../../platform_shared/serial_frame_vectors.json'),
        'utf-8'
    )
) as { vectors: Vector[] }

const fromHex = (hex: string) => Uint8Array.from(hex.match(/../g) ?? [], byte => parseInt(byte, 16))
const toHex = (bytes: Uint8Array) => Buffer.from(bytes).toString('hex')
const ascii = (text: string) => new TextEncoder().encode(text)
const concat = (...parts: Uint8Array[]) => Uint8Array.from(parts.flatMap(part => [...part]))

const chunkings: Record<string, (stream: Uint8Array) => Uint8Array[]> = {
    whole: stream => [stream],
    'byte by byte': stream => [...stream].map(byte => Uint8Array.of(byte)),
    'in 7-byte chunks': stream =>
        Array.from({ length: Math.ceil(stream.length / 7) }, (_, i) =>
            stream.subarray(i * 7, i * 7 + 7)
        )
}

const decode = (chunks: Uint8Array[]) => {
    const frames: string[] = []
    const lines: string[] = []
    const decoder = new SerialFrameDecoder({
        onFrame: message => frames.push(toHex(message)),
        onLogLine: line => lines.push(line)
    })
    chunks.forEach(chunk => decoder.feed(chunk))
    return { frames, lines }
}

describe('serial framing against the shared vectors', () => {
    it('has vectors to check', () => {
        expect(vectors.length).toBeGreaterThanOrEqual(7)
    })

    for (const vector of vectors) {
        for (const [chunking, split] of Object.entries(chunkings)) {
            it(`decodes "${vector.name}" fed ${chunking}`, () => {
                const { frames, lines } = decode(split(fromHex(vector.stream)))

                expect(frames).toEqual(vector.frames)
                if (vector.lines !== null) expect(lines).toEqual(vector.lines)
            })
        }

        if (vector.frames.length === 1 && vector.lines?.length === 0) {
            it(`encodes "${vector.name}" to the vector's stream`, () => {
                expect(toHex(encodeSerialFrame(fromHex(vector.frames[0])))).toBe(vector.stream)
            })
        }
    }
})

describe('serial framing', () => {
    it('finds the frames again after starting in the middle of one', () => {
        const message = (seed: number) =>
            Uint8Array.from({ length: 300 }, (_, i) => (i * 13 + seed) % 256)
        const cut = encodeSerialFrame(message(1))
        const stream = concat(
            cut.subarray(cut.length / 2),
            ascii('I (50) main: Booting robot\r\nI (60) wifi: Got IP\n'),
            encodeSerialFrame(message(2)),
            encodeSerialFrame(message(3)),
            ascii('I (70) app: done\n'),
            encodeSerialFrame(message(4))
        )

        for (const split of Object.values(chunkings)) {
            const { frames, lines } = decode(split(stream))

            expect(frames).toEqual([message(2), message(3), message(4)].map(toHex))
            expect(lines.slice(-3)).toEqual([
                'I (50) main: Booting robot',
                'I (60) wifi: Got IP',
                'I (70) app: done'
            ])
        }
    })

    it('drops only the frame with a corrupt byte, and shows it as log text', () => {
        const corrupt = encodeSerialFrame(ascii('first'))
        corrupt[3] ^= 0x20
        const { frames, lines } = decode([
            concat(corrupt, encodeSerialFrame(ascii('second')), ascii('after\n'))
        ])

        expect(frames).toEqual([toHex(ascii('second'))])
        expect(lines).toHaveLength(2)
        expect(lines[1]).toBe('after')
    })

    it('hands a long run without a delimiter over as log text before the next 0x00', () => {
        // A stray 0x00 in the log must not hold back everything after it until the next frame.
        const longLine = 'x'.repeat(SERIAL_MAX_MESSAGE + 100)
        const lines: string[] = []
        const decoder = new SerialFrameDecoder({ onFrame: () => {}, onLogLine: l => lines.push(l) })

        decoder.feed(concat(Uint8Array.of(0), ascii(`${longLine}\nnext line\n`)))

        expect(lines).toEqual([longLine, 'next line'])
    })

    it('round-trips a message of the largest size, zeros included', () => {
        const message = Uint8Array.from({ length: SERIAL_MAX_MESSAGE }, (_, i) => (i % 3 ? i : 0))
        const { frames } = decode([encodeSerialFrame(message)])

        expect(frames).toEqual([toHex(message)])
    })

    it('refuses to encode a message larger than a frame carries', () => {
        expect(() => encodeSerialFrame(new Uint8Array(SERIAL_MAX_MESSAGE + 1))).toThrow(RangeError)
    })

    it('keeps an empty log line and strips a carriage return', () => {
        const { lines } = decode([ascii('a\r\n\r\nb\n')])

        expect(lines).toEqual(['a', '', 'b'])
    })
})
