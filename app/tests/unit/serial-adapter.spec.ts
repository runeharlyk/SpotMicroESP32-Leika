import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { get } from 'svelte/store'
import { Message } from '../../src/lib/platform_shared/message'
import {
    SERIAL_BAUD_RATE,
    createSerialTransport,
    serialPortOpen
} from '../../src/lib/transport/serial-adapter'
import { SerialFrameDecoder, encodeSerialFrame } from '../../src/lib/transport/serial-framing'
import { serialLog } from '../../src/lib/stores/serial-log'
import { FakeSerialPort, asPort, fakeSerial, pongs, serialRobot } from './fake-serial'

const ascii = (text: string) => new TextEncoder().encode(text)
const frameOf = (message: Message) => encodeSerialFrame(Message.encode(message).finish())

/** Splits `bytes` at arbitrary points, as a USB serial driver hands them over. */
const scatter = (bytes: Uint8Array, sizes: number[]) => {
    const chunks: Uint8Array[] = []
    for (let at = 0, i = 0; at < bytes.length; i++) {
        const size = sizes[i % sizes.length]
        chunks.push(bytes.slice(at, at + size))
        at += size
    }
    return chunks
}

const handlers = () => ({ onOpen: vi.fn(), onData: vi.fn(), onClose: vi.fn() })

describe('serial transport', () => {
    let port: FakeSerialPort
    let fake: ReturnType<typeof fakeSerial>

    beforeEach(() => {
        port = new FakeSerialPort()
        fake = fakeSerial([], port)
        vi.stubGlobal('navigator', { serial: fake.serial })
        serialLog.clear()
    })

    afterEach(() => vi.unstubAllGlobals())

    it('opens the picked port at the console speed with DTR and RTS released', async () => {
        const transport = createSerialTransport(handlers())
        await transport.connect()

        expect(port.baudRate).toBe(SERIAL_BAUD_RATE)
        expect(port.signals).toEqual([{ dataTerminalReady: false, requestToSend: false }])
        expect(transport.canAutoReconnect).toBe(true)
        expect(transport.maxFrameSize).toBe(4096)
        expect(get(serialPortOpen)).toBe(true)
        transport.close()
    })

    it('counts as open only once the robot answers its probe, which may take a boot', async () => {
        const events = handlers()
        const transport = createSerialTransport(events)
        await transport.connect()

        await vi.waitFor(() => expect(port.written.length).toBeGreaterThan(0))
        expect(events.onOpen).not.toHaveBeenCalled()
        expect(transport.isConnected()).toBe(false)
        transport.send(Message.encode(Message.create({ pingmsg: {} })).finish())
        const writtenBeforeOpen = port.written.length

        serialRobot(port, pongs)
        await vi.waitFor(() => expect(events.onOpen).toHaveBeenCalledTimes(1), { timeout: 2500 })
        expect(transport.isConnected()).toBe(true)
        // A send before the robot answered would only be lost while it boots.
        expect(writtenBeforeOpen).toBe(1)
        transport.close()
    })

    it('passes frames to onData and log lines to the log, however the bytes are chunked', async () => {
        const events = handlers()
        const transport = createSerialTransport(events)
        await transport.connect()
        port.deliver(frameOf(Message.create({ pongmsg: {} })))
        await vi.waitFor(() => expect(events.onOpen).toHaveBeenCalled())

        const reply = Message.create({
            correlationResponse: { correlationId: 7, statusCode: 200, errorMessage: '' }
        })
        const stream = Uint8Array.from([
            ...ascii('I (900) wifi: connecting\r\nI (950) wifi: Got IP 192.168.1.'),
            ...frameOf(reply),
            ...ascii('20\r\n'),
            ...frameOf(Message.create({ pongmsg: {} }))
        ])
        scatter(stream, [1, 5, 3, 64, 2]).forEach(chunk => port.deliver(chunk))

        await vi.waitFor(() => expect(events.onData).toHaveBeenCalledTimes(2))
        const first = Message.decode(new Uint8Array(events.onData.mock.calls[0][0] as ArrayBuffer))
        expect(first.correlationResponse?.correlationId).toBe(7)
        expect(get(serialLog).map(line => line.text)).toEqual([
            'I (900) wifi: connecting',
            'I (950) wifi: Got IP 192.168.1.',
            '20'
        ])
        transport.close()
    })

    it('writes a sent message as one framed, checksummed frame', async () => {
        const events = handlers()
        const transport = createSerialTransport(events)
        await transport.connect()
        port.deliver(frameOf(Message.create({ pongmsg: {} })))
        await vi.waitFor(() => expect(events.onOpen).toHaveBeenCalled())

        const request = Message.encode(
            Message.create({ correlationRequest: { correlationId: 3, featuresDataRequest: {} } })
        ).finish()
        const before = port.written.length
        transport.send(request)
        await vi.waitFor(() => expect(port.written.length).toBe(before + 1))

        const frames: Uint8Array[] = []
        new SerialFrameDecoder({ onFrame: f => frames.push(f), onLogLine: () => {} }).feed(
            port.written[before]
        )
        expect(frames).toEqual([request])
        transport.close()
    })

    it('closes the port, and a later transport reopens the granted port without asking', async () => {
        const first = createSerialTransport(handlers())
        await first.connect()
        first.close()

        const requestPort = vi.spyOn(fake.serial, 'requestPort')
        const events = handlers()
        const second = createSerialTransport(events, asPort(port))
        await second.connect()
        serialRobot(port, pongs)

        await vi.waitFor(() => expect(events.onOpen).toHaveBeenCalled(), { timeout: 2500 })
        expect(requestPort).not.toHaveBeenCalled()
        expect(port.opens).toBe(2)
        second.close()
        await vi.waitFor(() => expect(port.isOpen).toBe(false))
    })

    it('reports a port lost in a reset, and reopens the device when it comes back', async () => {
        const events = handlers()
        const transport = createSerialTransport(events)
        await transport.connect()

        fake.unplug(port)
        await vi.waitFor(() => expect(events.onClose).toHaveBeenCalledWith('close'))
        expect(get(serialPortOpen)).toBe(false)

        // A native USB board enumerates again as a new port of the same device.
        const returned = new FakeSerialPort()
        const reopened = handlers()
        const next = createSerialTransport(reopened, transport.port)
        const connecting = next.connect()
        fake.plugIn(returned)
        await connecting
        serialRobot(returned, pongs)

        await vi.waitFor(() => expect(reopened.onOpen).toHaveBeenCalled(), { timeout: 2500 })
        expect(returned.isOpen).toBe(true)
        next.close()
    })

    it('finds the device under its new port when it came back before the reopen', async () => {
        const transport = createSerialTransport(handlers())
        await transport.connect()
        fake.unplug(port)
        const returned = new FakeSerialPort()
        fake.plugIn(returned)

        const next = createSerialTransport(handlers(), transport.port)
        await next.connect()

        expect(next.port).toBe(returned)
        expect(returned.isOpen).toBe(true)
        next.close()
    })

    it('stops waiting for a lost port once closed', async () => {
        const next = createSerialTransport(handlers(), asPort(new FakeSerialPort()))
        const connecting = next.connect()
        next.close()

        await expect(connecting).rejects.toBeDefined()
    })

    it('refuses to connect without Web Serial', async () => {
        vi.stubGlobal('navigator', {})
        await expect(createSerialTransport(handlers()).connect()).rejects.toThrow(/Web Serial/)
    })
})
