import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import {
    createBleTransport,
    BLE_MAX_FRAME_SIZE,
    BLE_SERVICE_UUID
} from '$lib/transport/ble-adapter'

/**
 * These pin the wire contract against robot_comm_ble (ble_adapter.cpp): one protobuf frame per
 * write, one per notification, with no length prefix and no fragmentation. A mismatch here is
 * silent on the robot, which just drops the frame with a log line.
 */

type Listener = (event: unknown) => void

function fakeBluetooth() {
    const writes: Uint8Array[] = []
    const listeners = new Map<string, Listener[]>()
    let notify: ((bytes: Uint8Array) => void) | undefined

    const characteristic = (uuid: string) => ({
        uuid,
        writeValueWithoutResponse: (data: Uint8Array) => {
            writes.push(new Uint8Array(data))
            return Promise.resolve()
        },
        startNotifications: () => Promise.resolve(),
        addEventListener: (type: string, listener: Listener) => {
            if (type !== 'characteristicvaluechanged') return
            notify = (bytes: Uint8Array) =>
                listener({ target: { value: new DataView(bytes.buffer.slice(0)) } })
        }
    })

    const device = {
        name: 'leika',
        gatt: {
            connected: true,
            connect: () =>
                Promise.resolve({
                    getPrimaryService: (uuid: string) => {
                        expect(uuid).toBe(BLE_SERVICE_UUID)
                        return Promise.resolve({
                            getCharacteristic: (id: string) => Promise.resolve(characteristic(id))
                        })
                    }
                }),
            disconnect: () => {
                device.gatt.connected = false
            }
        },
        addEventListener: (type: string, listener: Listener) => {
            listeners.set(type, [...(listeners.get(type) ?? []), listener])
        },
        removeEventListener: () => {}
    }

    return {
        writes,
        emitNotification: (bytes: Uint8Array) => notify?.(bytes),
        navigatorStub: { requestDevice: () => Promise.resolve(device) }
    }
}

describe('BLE transport', () => {
    let fake: ReturnType<typeof fakeBluetooth>

    beforeEach(() => {
        fake = fakeBluetooth()
        vi.stubGlobal('navigator', { bluetooth: fake.navigatorStub })
    })

    afterEach(() => vi.unstubAllGlobals())

    const connected = async () => {
        const handlers = { onOpen: vi.fn(), onData: vi.fn(), onClose: vi.fn() }
        const transport = createBleTransport(handlers)
        await transport.connect()
        return { transport, handlers }
    }

    it('reports its frame cap and that it cannot silently redial', async () => {
        const { transport } = await connected()

        expect(transport.kind).toBe('bluetooth')
        expect(transport.maxFrameSize).toBe(BLE_MAX_FRAME_SIZE)
        expect(transport.canAutoReconnect).toBe(false)
    })

    it('writes the payload verbatim, with no length prefix', async () => {
        const { transport } = await connected()
        const payload = new Uint8Array([1, 2, 3, 4, 5])

        transport.send(payload)
        await vi.waitFor(() => expect(fake.writes).toHaveLength(1))

        expect(Array.from(fake.writes[0])).toEqual([1, 2, 3, 4, 5])
    })

    it('refuses a frame larger than the link carries rather than letting the robot drop it', async () => {
        const { transport } = await connected()
        const warn = vi.spyOn(console, 'warn').mockImplementation(() => {})

        transport.send(new Uint8Array(BLE_MAX_FRAME_SIZE + 1))

        expect(fake.writes).toHaveLength(0)
        expect(warn).toHaveBeenCalled()
        warn.mockRestore()
    })

    it('accepts a frame exactly at the cap', async () => {
        const { transport } = await connected()

        transport.send(new Uint8Array(BLE_MAX_FRAME_SIZE))
        await vi.waitFor(() => expect(fake.writes).toHaveLength(1))

        expect(fake.writes[0]).toHaveLength(BLE_MAX_FRAME_SIZE)
    })

    it('delivers each notification as one complete frame', async () => {
        const { handlers } = await connected()

        fake.emitNotification(new Uint8Array([9, 8, 7]))

        expect(handlers.onData).toHaveBeenCalledTimes(1)
        const received = new Uint8Array(handlers.onData.mock.calls[0][0] as ArrayBuffer)
        expect(Array.from(received)).toEqual([9, 8, 7])
    })

    it('copies the outgoing buffer so a caller reusing it cannot corrupt the write', async () => {
        const { transport } = await connected()
        const payload = new Uint8Array([1, 2, 3])

        transport.send(payload)
        payload.fill(0)
        await vi.waitFor(() => expect(fake.writes).toHaveLength(1))

        expect(Array.from(fake.writes[0])).toEqual([1, 2, 3])
    })
})
