import { get, writable } from 'svelte/store'
import { socket } from '$lib/stores/socket'
import {
    TelemetryBatch,
    TelemetryHeader,
    TelemetryNetwork,
    type MessageFns
} from '$lib/platform_shared/message'
import { RecordingWriter, loadRecording, type Recording } from './leika-recording'
import { summarize, type TelemetryStats } from './telemetry-stats'

/** Past this size a recording still grows, but the page warns that it is holding a lot. */
export const LARGE_RECORDING_BYTES = 256 * 2 ** 20

export interface FinishedRecording {
    blob: Blob
    filename: string
    recording?: Recording
    stats?: TelemetryStats
    error?: string
}

export interface RecorderState {
    recording: boolean
    bytes: number
    frames: number
    gaps: number
    finished?: FinishedRecording
}

const hostUs = () => Math.round((performance.timeOrigin + performance.now()) * 1000)

const stamp = (date: Date) => {
    const pad = (value: number) => String(value).padStart(2, '0')
    return (
        `${date.getFullYear()}${pad(date.getMonth() + 1)}${pad(date.getDate())}-` +
        `${pad(date.getHours())}${pad(date.getMinutes())}${pad(date.getSeconds())}`
    )
}

/**
 * Records the robot's telemetry as simulation/record.py does: the header the robot sends on subscribing to the
 * batches, every batch and network sample as it arrived, and a gap wherever the link dropped and came back.
 */
function createRecorder() {
    const state = writable<RecorderState>({ recording: false, bytes: 0, frames: 0, gaps: 0 })
    let writer: RecordingWriter | undefined
    let startedAt = new Date()
    let stopListening: (() => void)[] = []

    const record = <T>(kind: MessageFns<T>) =>
        socket.on(kind, (_data, frame) => {
            if (!writer) return
            writer.write(new Uint8Array(frame), hostUs())
            state.update(s => ({ ...s, bytes: writer!.size, frames: s.frames + 1 }))
        })

    function start() {
        if (writer) return
        writer = new RecordingWriter()
        startedAt = new Date()
        let linkWasUp = get(socket)
        state.set({ recording: true, bytes: writer.size, frames: 0, gaps: 0 })
        stopListening = [
            socket.onEvent('open', () => {
                if (linkWasUp && writer) {
                    writer.gap(hostUs())
                    state.update(s => ({ ...s, bytes: writer!.size, gaps: s.gaps + 1 }))
                }
                linkWasUp = true
            }),
            // The header listener comes first: subscribing to the batches is what makes the robot send it.
            record(TelemetryHeader),
            record(TelemetryBatch),
            record(TelemetryNetwork)
        ]
    }

    async function stop() {
        if (!writer) return
        stopListening.forEach(stopOne => stopOne())
        stopListening = []
        const blob = writer.blob()
        writer = undefined
        const filename = `recording-${stamp(startedAt)}.leika`
        state.update(s => ({ ...s, recording: false, finished: { blob, filename } }))
        let report: Partial<FinishedRecording>
        try {
            const recording = loadRecording(new Uint8Array(await blob.arrayBuffer()))
            report = { recording, stats: summarize(recording) }
        } catch (error) {
            report = { error: String(error) }
        }
        state.update(s => ({ ...s, finished: { blob, filename, ...report } }))
    }

    return { subscribe: state.subscribe, start, stop }
}

export const recorder = createRecorder()
