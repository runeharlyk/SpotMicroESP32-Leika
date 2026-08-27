import { describe, it, expect, vi, beforeEach } from 'vitest'
import { get } from 'svelte/store'

const connectBluetooth = vi.fn()
const error = vi.fn()

vi.mock('$lib/stores/socket', () => ({ socket: { connectBluetooth } }))
vi.mock('$lib/components/toasts/notifications', () => ({ notifications: { error } }))

const { pairing, startPairing } = await import('$lib/stores/pairing')

describe('startPairing', () => {
    beforeEach(() => {
        connectBluetooth.mockReset()
        error.mockReset()
    })

    it('reports success and leaves the pairing flag down', async () => {
        connectBluetooth.mockResolvedValue(undefined)

        await expect(startPairing()).resolves.toBe(true)
        expect(get(pairing)).toBe(false)
    })

    it('raises the pairing flag while the chooser is open', async () => {
        let flagDuringCall: boolean | undefined
        connectBluetooth.mockImplementation(() => {
            flagDuringCall = get(pairing)
            return Promise.resolve()
        })

        await startPairing()

        expect(flagDuringCall).toBe(true)
    })

    // Dismissing the browser's own device chooser rejects with NotFoundError. Alarming the user
    // about a dialog they closed themselves would be noise.
    it('stays quiet when the user dismisses the device chooser', async () => {
        connectBluetooth.mockRejectedValue(new DOMException('cancelled', 'NotFoundError'))

        await expect(startPairing()).resolves.toBe(false)
        expect(error).not.toHaveBeenCalled()
        expect(get(pairing)).toBe(false)
    })

    it('surfaces a genuine failure', async () => {
        connectBluetooth.mockRejectedValue(new Error('GATT unreachable'))

        await expect(startPairing()).resolves.toBe(false)
        expect(error).toHaveBeenCalledOnce()
        expect(get(pairing)).toBe(false)
    })
})
