import { describe, it, expect, vi, afterEach } from 'vitest'
import { get } from 'svelte/store'
import { Message } from '../../src/lib/platform_shared/message'
import { socket } from '../../src/lib/stores/socket'
import { FakeSerialPort, fakeSerial, serialRobot } from './fake-serial'

// Answers pings and the features request, as the firmware does over serial.
const robot = (message: Message) => {
    if (message.pingmsg) return Message.create({ pongmsg: {} })
    const request = message.correlationRequest
    if (request?.featuresDataRequest) {
        return Message.create({
            correlationResponse: {
                correlationId: request.correlationId,
                statusCode: 200,
                errorMessage: '',
                featuresDataResponse: { robotName: 'leika' }
            }
        })
    }
}

describe.sequential('socket over USB serial', () => {
    afterEach(() => vi.unstubAllGlobals())

    it('answers requests over the port once the robot speaks', async () => {
        const port = new FakeSerialPort()
        const fake = fakeSerial([], port)
        vi.stubGlobal('navigator', { serial: fake.serial })
        serialRobot(port, robot)

        await socket.connectSerial()
        await vi.waitFor(() => expect(get(socket)).toBe(true), { timeout: 2500 })
        expect(get(socket.transport)).toBe('serial')

        const reply = await socket.request({ featuresDataRequest: {} })
        expect(reply.featuresDataResponse?.robotName).toBe('leika')
    })

    it('comes back by itself after the board resets and its port returns', async () => {
        const port = new FakeSerialPort()
        const fake = fakeSerial([], port)
        vi.stubGlobal('navigator', { serial: fake.serial })
        serialRobot(port, robot)
        const closed = vi.fn()
        const off = socket.onEvent('close', closed)

        await socket.connectSerial()
        await vi.waitFor(() => expect(get(socket)).toBe(true), { timeout: 2500 })

        fake.unplug(port)
        await vi.waitFor(() => expect(get(socket)).toBe(false))
        expect(closed).toHaveBeenCalledTimes(1)

        const returned = new FakeSerialPort()
        serialRobot(returned, robot)
        fake.plugIn(returned)

        await vi.waitFor(() => expect(get(socket)).toBe(true), { timeout: 5000 })
        expect(get(socket.transport)).toBe('serial')
        const reply = await socket.request({ featuresDataRequest: {} })
        expect(reply.featuresDataResponse?.robotName).toBe('leika')
        off()
    })

    it('rejects a cancelled port chooser and leaves no serial link behind', async () => {
        vi.stubGlobal('navigator', { serial: fakeSerial([]).serial })

        await expect(socket.connectSerial()).rejects.toMatchObject({ name: 'NotFoundError' })
        expect(get(socket)).toBe(false)
    })
})
