import { describe, it, expect, vi, afterEach } from 'vitest'
import { get } from 'svelte/store'
import { socket } from '../../src/lib/stores/socket'
import { BLE_SERVICE_UUID } from '../../src/lib/transport/ble-adapter'

type Listener = () => void

// A paired device whose GATT link the test can drop, as a robot walking out of range would.
function fakeBluetoothDevice() {
    const listeners: Listener[] = []
    const characteristic = {
        writeValueWithoutResponse: () => Promise.resolve(),
        startNotifications: () => Promise.resolve(),
        addEventListener: () => {}
    }
    const device = {
        name: 'leika',
        gatt: {
            connected: true,
            connect: () =>
                Promise.resolve({
                    getPrimaryService: (uuid: string) => {
                        expect(uuid).toBe(BLE_SERVICE_UUID)
                        return Promise.resolve({
                            getCharacteristic: () => Promise.resolve(characteristic)
                        })
                    }
                }),
            disconnect: () => {
                device.gatt.connected = false
            }
        },
        addEventListener: (_type: string, listener: Listener) => listeners.push(listener),
        removeEventListener: () => {}
    }
    return {
        requestDevice: () => Promise.resolve(device),
        dropLink: () => {
            device.gatt.connected = false
            listeners.forEach(listener => listener())
        }
    }
}

describe('socket over Bluetooth', () => {
    afterEach(() => vi.unstubAllGlobals())

    it('reports a dropped Bluetooth link once instead of recursing through close', async () => {
        const bluetooth = fakeBluetoothDevice()
        vi.stubGlobal('navigator', { ...navigator, bluetooth })
        const closed = vi.fn()
        socket.onEvent('close', closed)

        await socket.connectBluetooth()
        expect(get(socket)).toBe(true)

        expect(() => bluetooth.dropLink()).not.toThrow()
        expect(get(socket)).toBe(false)
        expect(closed).toHaveBeenCalledTimes(1)
    })
})
