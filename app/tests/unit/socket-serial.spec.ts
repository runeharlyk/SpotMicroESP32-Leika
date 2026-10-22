import { describe, it, expect, vi, afterEach } from 'vitest'
import { get } from 'svelte/store'
import { ControllerData, IMUData, Message } from '../../src/lib/platform_shared/message'
import { MESSAGE_KEY_TO_TAG, socket } from '../../src/lib/stores/socket'
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

    // The robot's native USB port carries the whole protocol, as its WebSocket does.
    it('subscribes to a stream and drives over the port', async () => {
        const port = new FakeSerialPort()
        vi.stubGlobal('navigator', { serial: fakeSerial([], port).serial })
        const imuTag = MESSAGE_KEY_TO_TAG.get('imu')
        const reading = { x: 1, y: 2, z: 3, heading: 90, altitude: 0, bmpTemp: 0, pressure: 0 }
        const { received } = serialRobot(port, message =>
            message.subNotif?.tag === imuTag ? Message.create({ imu: reading }) : robot(message)
        )

        await socket.connectSerial()
        await vi.waitFor(() => expect(get(socket)).toBe(true), { timeout: 2500 })
        const readings: IMUData[] = []
        const off = socket.on(IMUData, data => readings.push(data))
        await vi.waitFor(() => expect(readings).toEqual([reading]))

        const input = ControllerData.create({ left: { x: 0, y: 0.5 }, height: 0.75 })
        socket.emit(ControllerData, input)
        await vi.waitFor(() =>
            expect(received.map(message => message.controllerData).filter(Boolean)).toEqual([input])
        )
        off()
    })

    // A slow link queued every stick frame behind the one being written, so the robot walked on the past; it now hears
    // the newest once the write completes, and a released stick's stop always reaches it.
    it('sends only the newest sticks once a slow write completes', async () => {
        const port = new FakeSerialPort()
        vi.stubGlobal('navigator', { serial: fakeSerial([], port).serial })
        const { received } = serialRobot(port, robot)
        await socket.connectSerial()
        await vi.waitFor(() => expect(get(socket)).toBe(true), { timeout: 2500 })
        const sticks = (y: number) => ControllerData.create({ left: { x: 0, y }, height: 0.5 })
        const heard = () =>
            received.map(message => message.controllerData?.left?.y).filter(y => y !== undefined)
        socket.emit(ControllerData, sticks(0.5))
        await vi.waitFor(() => expect(heard()).toEqual([0.5]))

        port.hold()
        for (const y of [0.25, 0.75, 0.125, 0]) socket.emit(ControllerData, sticks(y))
        await vi.waitFor(() => expect(heard()).toEqual([0.5, 0.25]))
        port.release()

        await vi.waitFor(() => expect(heard()).toEqual([0.5, 0.25, 0]))
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
