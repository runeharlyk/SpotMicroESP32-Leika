import { describe, expect, it } from 'vitest'
import { LatestFrames } from '../../src/lib/transport/latest-frames'

const STICKS = 1
const OTHER = 2

// A link that is busy while a write is under way, as the BLE and serial adapters are.
const slowLink = () => {
    const written: number[][] = []
    let writing = false
    return {
        written,
        link: { busy: () => writing, write: (data: Uint8Array) => written.push([...data]) },
        startWriting: () => (writing = true),
        finishWriting: () => (writing = false)
    }
}

describe('LatestFrames', () => {
    it('writes a frame at once while nothing is being written', () => {
        const { written, link } = slowLink()
        new LatestFrames(link).offer(STICKS, Uint8Array.of(1))
        expect(written).toEqual([[1]])
    })

    it('sends only the newest frame of a kind once the write under way completes', () => {
        const { written, link, startWriting, finishWriting } = slowLink()
        const frames = new LatestFrames(link)
        startWriting()
        frames.offer(STICKS, Uint8Array.of(1))
        frames.offer(STICKS, Uint8Array.of(2))
        frames.offer(STICKS, Uint8Array.of(3))
        expect(written).toEqual([])
        finishWriting()
        frames.drained()
        expect(written).toEqual([[3]])
    })

    it('keeps the newest frame of each kind apart', () => {
        const { written, link, startWriting, finishWriting } = slowLink()
        const frames = new LatestFrames(link)
        startWriting()
        frames.offer(STICKS, Uint8Array.of(1))
        frames.offer(OTHER, Uint8Array.of(9))
        frames.offer(STICKS, Uint8Array.of(2))
        finishWriting()
        frames.drained()
        expect(written).toEqual([[2], [9]])
    })

    it('sends a released stick that arrives mid-write, so the robot always hears the stop', () => {
        const { written, link, startWriting, finishWriting } = slowLink()
        const frames = new LatestFrames(link)
        frames.offer(STICKS, Uint8Array.of(1))
        startWriting()
        frames.offer(STICKS, Uint8Array.of(0))
        finishWriting()
        frames.drained()
        expect(written.at(-1)).toEqual([0])
    })

    it('forgets what waits when the link drops, so a reconnect does not move the robot with the past', () => {
        const { written, link, startWriting, finishWriting } = slowLink()
        const frames = new LatestFrames(link)
        startWriting()
        frames.offer(STICKS, Uint8Array.of(1))
        frames.clear()
        finishWriting()
        frames.drained()
        expect(written).toEqual([])
    })
})
