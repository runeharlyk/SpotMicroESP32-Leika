import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { createBleTransport, BLE_SERVICE_UUID } from '$lib/transport/ble-adapter'
import { BLE_MAX_OUTGOING, BLE_WRITE_CHUNK, encodeBleFrame } from '$lib/transport/ble-framing'

/**
 * These pin the wire contract against the firmware's nimble_link.cpp: each message as a uint16
 * little-endian length and the message, in writes of at most BLE_WRITE_CHUNK bytes, and rebuilt
 * from notifications chunked the same way.
 */

type Listener = (event: unknown) => void

function fakeBluetooth() {
    const writes: Uint8Array[] = []
    // Held writes complete only once released, as a slow link's do.
    let holding = false
    let held: (() => void)[] = []
    const listeners = new Map<string, Listener[]>()
    let notify: ((bytes: Uint8Array) => void) | undefined

    const characteristic = (uuid: string) => ({
        uuid,
        writeValueWithoutResponse: (data: Uint8Array) => {
            writes.push(new Uint8Array(data))
            return holding ? new Promise<void>(resolve => held.push(resolve)) : Promise.resolve()
        },
        startNotifications: () => Promise.resolve(),
        addEventListener: (type: string, listener: Listener) => {
            if (type !== 'characteristicvaluechanged') return
            notify = (bytes: Uint8Array) =>
                listener({ target: { value: new DataView(bytes.buffer.slice(0)) } })
        },
        removeEventListener: () => (notify = undefined)
    })

    const device = {
        name: 'leika',
        gatt: {
            connected: false,
            connect: () => {
                device.gatt.connected = true
                return Promise.resolve({
                    getPrimaryService: (uuid: string) => {
                        expect(uuid).toBe(BLE_SERVICE_UUID)
                        return Promise.resolve({
                            getCharacteristic: (id: string) => Promise.resolve(characteristic(id))
                        })
                    }
                })
            },
            disconnect: () => {
                device.gatt.connected = false
            }
        },
        addEventListener: (type: string, listener: Listener) => {
            listeners.set(type, [...(listeners.get(type) ?? []), listener])
        },
        removeEventListener: () => {}
    }

    const requestDevice = vi.fn(() => Promise.resolve(device))

    return {
        device,
        requestDevice,
        writes,
        hold: () => (holding = true),
        release: () => {
            holding = false
            held.forEach(resolve => resolve())
            held = []
        },
        emitNotification: (bytes: Uint8Array) => notify?.(bytes),
        navigatorStub: { requestDevice }
    }
}

describe('BLE transport', () => {
    let fake: ReturnType<typeof fakeBluetooth>

    beforeEach(() => {
        fake = fakeBluetooth()
        vi.stubGlobal('navigator', { bluetooth: fake.navigatorStub })
    })

    afterEach(() => vi.unstubAllGlobals())

    const handlers = () => ({
        onOpen: vi.fn(),
        onData: vi.fn(),
        onClose: vi.fn(),
        onDrained: vi.fn()
    })

    const connected = async () => {
        const watched = handlers()
        const transport = createBleTransport(watched)
        await transport.connect()
        return { transport, handlers: watched }
    }

    const received = (handlers: { onData: ReturnType<typeof vi.fn> }) =>
        handlers.onData.mock.calls.map(([data]) => Array.from(new Uint8Array(data as ArrayBuffer)))

    it('reports the largest message the robot takes', async () => {
        const { transport } = await connected()

        expect(transport.kind).toBe('bluetooth')
        expect(transport.maxFrameSize).toBe(BLE_MAX_OUTGOING)
    })

    // Only the chooser needs a user gesture; a device chosen once reconnects without it.
    it('can redial only once a device was chosen, and then without the chooser', async () => {
        const first = createBleTransport(handlers())
        expect(first.canAutoReconnect).toBe(false)
        await first.connect()
        expect(first.canAutoReconnect).toBe(true)
        first.close()

        const again = createBleTransport(handlers(), first.device)
        await again.connect()

        expect(again.isConnected()).toBe(true)
        expect(fake.requestDevice).toHaveBeenCalledTimes(1)
    })

    it('writes a small message as one write behind its little-endian length', async () => {
        const { transport } = await connected()

        transport.send(new Uint8Array([1, 2, 3, 4, 5]))
        await vi.waitFor(() => expect(fake.writes).toHaveLength(1))

        expect(Array.from(fake.writes[0])).toEqual([5, 0, 1, 2, 3, 4, 5])
    })

    it('splits a long message into writes no longer than a chunk, in order', async () => {
        const { transport } = await connected()
        const message = Uint8Array.from({ length: 730 }, (_, i) => i % 256)

        transport.send(message)
        await vi.waitFor(() => expect(fake.writes).toHaveLength(Math.ceil(732 / BLE_WRITE_CHUNK)))

        fake.writes.forEach(write => expect(write.length).toBeLessThanOrEqual(BLE_WRITE_CHUNK))
        const wire = fake.writes.flatMap(write => Array.from(write))
        expect(wire).toEqual([730 & 0xff, 730 >> 8, ...message])
    })

    it('is busy while a write is under way and says once the last one completes', async () => {
        const { transport, handlers } = await connected()
        expect(transport.busy()).toBe(false)

        fake.hold()
        transport.send(Uint8Array.of(1))
        transport.send(Uint8Array.of(2))
        expect(transport.busy()).toBe(true)
        await vi.waitFor(() => expect(fake.writes).toHaveLength(1))
        expect(handlers.onDrained).not.toHaveBeenCalled()

        fake.release()
        await vi.waitFor(() => expect(handlers.onDrained).toHaveBeenCalledTimes(1))
        expect(fake.writes).toHaveLength(2)
        expect(transport.busy()).toBe(false)
    })

    it('refuses a message larger than the robot takes', async () => {
        const { transport } = await connected()
        const warn = vi.spyOn(console, 'warn').mockImplementation(() => {})

        transport.send(new Uint8Array(BLE_MAX_OUTGOING + 1))

        expect(fake.writes).toHaveLength(0)
        expect(warn).toHaveBeenCalled()
        warn.mockRestore()
    })

    it('hands over a message notified in several chunks once, whole', async () => {
        const { handlers } = await connected()
        const message = Uint8Array.from({ length: 500 }, (_, i) => (i * 7) % 256)

        for (const chunk of encodeBleFrame(message, 244)) fake.emitNotification(chunk)

        expect(received(handlers)).toEqual([Array.from(message)])
    })

    it('still hands over the next message when handling one throws', async () => {
        const { handlers } = await connected()
        const error = vi.spyOn(console, 'error').mockImplementation(() => {})
        handlers.onData.mockImplementationOnce(() => {
            throw new Error('unknown message')
        })

        fake.emitNotification(Uint8Array.of(1, 0, 9))
        fake.emitNotification(Uint8Array.of(1, 0, 8))
        fake.emitNotification(Uint8Array.of(1, 0, 7))

        expect(received(handlers)).toEqual([[9], [8], [7]])
        error.mockRestore()
    })

    // A notification lost mid-message: a length prefix alone would read the next messages as its rest.
    it('ends a connection whose stream fell out of step, handing over nothing of it', async () => {
        const { transport, handlers } = await connected()
        const warn = vi.spyOn(console, 'warn').mockImplementation(() => {})
        const [cutShort] = encodeBleFrame(new Uint8Array(300).fill(1), 182)

        fake.emitNotification(cutShort)
        for (let i = 0; i < 4; i++) {
            for (const chunk of encodeBleFrame(new Uint8Array(100).fill(i + 2), 182))
                fake.emitNotification(chunk)
        }

        expect(handlers.onData).not.toHaveBeenCalled()
        expect(handlers.onClose).toHaveBeenCalledTimes(1)
        expect(handlers.onClose.mock.calls[0][0]).toBe('error')
        expect(fake.device.gatt.connected).toBe(false)
        expect(transport.isConnected()).toBe(false)
        warn.mockRestore()
    })

    it('copies the outgoing buffer so a caller reusing it cannot corrupt the write', async () => {
        const { transport } = await connected()
        const payload = new Uint8Array([1, 2, 3])

        transport.send(payload)
        payload.fill(0)
        await vi.waitFor(() => expect(fake.writes).toHaveLength(1))

        expect(Array.from(fake.writes[0])).toEqual([3, 0, 1, 2, 3])
    })
})
