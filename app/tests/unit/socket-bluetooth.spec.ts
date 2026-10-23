import { describe, it, expect, vi, afterEach, beforeEach } from 'vitest'
import { get } from 'svelte/store'
import { createWebSocket } from '../../src/lib/stores/socket'
import { BLE_RECONNECT_ATTEMPTS, BLE_SERVICE_UUID } from '../../src/lib/transport/ble-adapter'

type Listener = () => void

// A paired device whose GATT link the test can drop, as a robot walking out of range would.
function fakeBluetoothDevice() {
    let listeners: Listener[] = []
    let reachable = true
    const characteristic = {
        writeValueWithoutResponse: () => Promise.resolve(),
        startNotifications: () => Promise.resolve(),
        addEventListener: () => {},
        removeEventListener: () => {}
    }
    const device = {
        name: 'leika',
        gatt: {
            connected: false,
            connect: vi.fn(() => {
                if (!reachable) return Promise.reject(new Error('Bluetooth device is out of range'))
                device.gatt.connected = true
                return Promise.resolve({
                    getPrimaryService: (uuid: string) => {
                        expect(uuid).toBe(BLE_SERVICE_UUID)
                        return Promise.resolve({
                            getCharacteristic: () => Promise.resolve(characteristic)
                        })
                    }
                })
            }),
            disconnect: () => {
                device.gatt.connected = false
            }
        },
        addEventListener: (_type: string, listener: Listener) => listeners.push(listener),
        removeEventListener: (_type: string, listener: Listener) =>
            (listeners = listeners.filter(held => held !== listener))
    }
    return {
        requestDevice: vi.fn(() => Promise.resolve(device)),
        connects: () => device.gatt.connect.mock.calls.length,
        goOutOfRange: () => (reachable = false),
        dropLink: () => {
            device.gatt.connected = false
            listeners.forEach(listener => listener())
        }
    }
}

describe('socket over Bluetooth', () => {
    let bluetooth: ReturnType<typeof fakeBluetoothDevice>

    beforeEach(() => {
        vi.useFakeTimers()
        bluetooth = fakeBluetoothDevice()
        vi.stubGlobal('navigator', { ...navigator, bluetooth })
    })

    afterEach(() => {
        vi.useRealTimers()
        vi.unstubAllGlobals()
        vi.restoreAllMocks()
    })

    it('reports a dropped Bluetooth link once instead of recursing through close', async () => {
        const socket = createWebSocket()
        const closed = vi.fn()
        socket.onEvent('close', closed)

        await socket.connectBluetooth()
        expect(get(socket)).toBe(true)

        expect(() => bluetooth.dropLink()).not.toThrow()
        expect(get(socket)).toBe(false)
        expect(closed).toHaveBeenCalledTimes(1)
    })

    it('reconnects the chosen device after a drop, without the chooser', async () => {
        const socket = createWebSocket()
        await socket.connectBluetooth()

        bluetooth.dropLink()
        await vi.advanceTimersByTimeAsync(1000)

        expect(get(socket)).toBe(true)
        expect(bluetooth.connects()).toBe(2)
        expect(bluetooth.requestDevice).toHaveBeenCalledTimes(1)
    })

    it('gives up after a bounded number of failed reconnects', async () => {
        const warn = vi.spyOn(console, 'warn').mockImplementation(() => {})
        const socket = createWebSocket()
        await socket.connectBluetooth()

        bluetooth.goOutOfRange()
        bluetooth.dropLink()
        await vi.advanceTimersByTimeAsync(10 * 60_000)

        expect(bluetooth.connects()).toBe(1 + BLE_RECONNECT_ATTEMPTS)
        expect(get(socket)).toBe(false)
        expect(warn).toHaveBeenCalledWith(expect.stringMatching(/gave up/))
    })
})
