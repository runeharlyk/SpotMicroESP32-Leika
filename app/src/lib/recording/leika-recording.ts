import {
    Message,
    type TelemetryHeader,
    type TelemetryNetwork,
    type TickSample
} from '$lib/platform_shared/message'

/**
 * The .leika format of simulation/src/sim/recording.py, so its loader and the report read what the app records:
 * b"LEIKAREC", uint16 version 1, then records of uint64 host receive time (us), uint32 length and one encoded
 * socket_message.Message; a record of length 0 marks a gap where the link dropped. All integers are little-endian.
 */
export const MAGIC = new TextEncoder().encode('LEIKAREC')
export const VERSION = 1
const PREAMBLE_BYTES = MAGIC.length + 2
const RECORD_HEADER_BYTES = 12

// Parts gather into a Blob once they reach this size: the browser may keep a Blob out of the page's heap.
const CHUNK_BYTES = 1 << 20

/** Appends frames as they arrive; `size` counts every byte written, the preamble included. */
export class RecordingWriter {
    private chunks: Blob[] = []
    private parts: Uint8Array<ArrayBuffer>[] = []
    private partBytes = 0
    size = 0

    constructor() {
        const preamble = new Uint8Array(PREAMBLE_BYTES)
        preamble.set(MAGIC)
        new DataView(preamble.buffer).setUint16(MAGIC.length, VERSION, true)
        this.push(preamble)
    }

    write(frame: Uint8Array<ArrayBuffer>, hostRxUs: number) {
        this.push(recordHeader(hostRxUs, frame.length))
        this.push(frame)
    }

    gap(hostRxUs: number) {
        this.push(recordHeader(hostRxUs, 0))
    }

    blob(): Blob {
        this.flush()
        return new Blob(this.chunks, { type: 'application/octet-stream' })
    }

    private push(part: Uint8Array<ArrayBuffer>) {
        this.parts.push(part)
        this.partBytes += part.length
        this.size += part.length
        if (this.partBytes >= CHUNK_BYTES) this.flush()
    }

    private flush() {
        if (this.parts.length === 0) return
        this.chunks.push(new Blob(this.parts))
        this.parts = []
        this.partBytes = 0
    }
}

function recordHeader(hostRxUs: number, length: number) {
    const header = new Uint8Array(RECORD_HEADER_BYTES)
    const view = new DataView(header.buffer)
    view.setBigUint64(0, BigInt(hostRxUs), true)
    view.setUint32(8, length, true)
    return header
}

export function* readRecords(data: Uint8Array): Generator<[hostRxUs: number, frame: Uint8Array]> {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
    if (data.length < PREAMBLE_BYTES || MAGIC.some((byte, i) => data[i] !== byte)) {
        throw new Error('not a Leika recording')
    }
    const version = view.getUint16(MAGIC.length, true)
    if (version !== VERSION) throw new Error(`recording version ${version}, expected ${VERSION}`)
    let offset = PREAMBLE_BYTES
    while (offset + RECORD_HEADER_BYTES <= data.length) {
        const hostRxUs = Number(view.getBigUint64(offset, true))
        const length = view.getUint32(offset + 8, true)
        const start = offset + RECORD_HEADER_BYTES
        if (start + length > data.length) return // the recorder stopped in the middle of this record
        yield [hostRxUs, data.subarray(start, start + length)]
        offset = start + length
    }
}

export interface Recording {
    header?: TelemetryHeader
    ticks: TickSample[]
    network: TelemetryNetwork[]
    batchRxUs: number[]
    linkGaps: number
    missingBatches: number
    missingTicks: number
    droppedTicks: number
    reboots: number
}

export function loadRecording(data: Uint8Array): Recording {
    let header: TelemetryHeader | undefined
    const ticks: TickSample[] = []
    const network: TelemetryNetwork[] = []
    const batchRxUs: number[] = []
    const batchSeqs: number[] = []
    const batchDropped: number[] = []
    let linkGaps = 0
    for (const [hostRxUs, frame] of readRecords(data)) {
        if (frame.length === 0) {
            linkGaps++
            continue
        }
        const message = Message.decode(frame)
        if (message.telemetryHeader) {
            header = message.telemetryHeader
        } else if (message.telemetryBatch) {
            const batch = message.telemetryBatch
            batchSeqs.push(batch.batchSeq)
            batchRxUs.push(hostRxUs)
            batchDropped.push(batch.droppedTicks)
            ticks.push(...batch.ticks)
        } else if (message.telemetryNetwork) {
            network.push(message.telemetryNetwork)
        }
    }
    return {
        header,
        ticks,
        network,
        batchRxUs,
        linkGaps,
        missingBatches: missing(batchSeqs),
        missingTicks: missing(ticks.map(tick => tick.seq)),
        droppedTicks: droppedWhileRecording(batchSeqs, batchDropped),
        reboots: restarts(batchSeqs).length
    }
}

/** Indices where a counter starts over, as the robot's do after a reboot. */
function restarts(sequence: number[]) {
    return sequence.flatMap((value, i) => (i > 0 && value <= sequence[i - 1] ? [i] : []))
}

/** Numbers skipped by a counter; a counter that starts over after a reboot skips nothing. */
function missing(sequence: number[]) {
    let skipped = 0
    for (let i = 1; i < sequence.length; i++) {
        const step = sequence[i] - sequence[i - 1]
        if (step > 0) skipped += step - 1
    }
    return skipped
}

/**
 * The robot counts dropped ticks since its boot: the recording's share is what the count grew by, plus the whole
 * count of every boot that began during the recording.
 */
function droppedWhileRecording(batchSeqs: number[], droppedSinceBoot: number[]) {
    if (droppedSinceBoot.length === 0) return 0
    const bounds = [0, ...restarts(batchSeqs), droppedSinceBoot.length]
    let total = 0
    for (let i = 0; i + 1 < bounds.length; i++) {
        const segment = droppedSinceBoot.slice(bounds[i], bounds[i + 1])
        total += segment[segment.length - 1] - (i === 0 ? segment[0] : 0)
    }
    return total
}
