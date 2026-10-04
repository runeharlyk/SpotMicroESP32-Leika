import { Message } from '../../src/lib/platform_shared/message'
import { SerialFrameDecoder, encodeSerialFrame } from '../../src/lib/transport/serial-framing'

const ESPRESSIF_USB = { usbVendorId: 0x303a, usbProductId: 0x1001 }

/**
 * A serial port as Web Serial hands it out: a byte stream in each direction while open, which
 * refuses a second open and a close while its streams are locked, and loses both when unplugged.
 */
export class FakeSerialPort extends EventTarget {
    readable: ReadableStream<Uint8Array> | null = null
    writable: WritableStream<Uint8Array> | null = null
    readonly written: Uint8Array[] = []
    readonly signals: SerialOutputSignals[] = []
    baudRate: number | undefined
    opens = 0
    onWrite: (bytes: Uint8Array) => void = () => {}
    private controller: ReadableStreamDefaultController<Uint8Array> | undefined

    constructor(private readonly info: SerialPortInfo = ESPRESSIF_USB) {
        super()
    }

    getInfo() {
        return this.info
    }

    async open(options: SerialOptions) {
        if (this.readable) throw new DOMException('The port is already open.', 'InvalidStateError')
        this.opens++
        this.baudRate = options.baudRate
        this.readable = new ReadableStream({ start: controller => (this.controller = controller) })
        this.writable = new WritableStream({
            write: chunk => {
                this.written.push(chunk.slice())
                this.onWrite(chunk.slice())
            }
        })
    }

    async setSignals(signals: SerialOutputSignals) {
        this.signals.push(signals)
    }

    async close() {
        if (!this.readable || !this.writable) {
            throw new DOMException('The port is already closed.', 'InvalidStateError')
        }
        if (this.readable.locked || this.writable.locked) throw new TypeError('The port is locked')
        await this.readable.cancel()
        await this.writable.close()
        this.readable = null
        this.writable = null
    }

    deliver(bytes: Uint8Array) {
        if (this.readable) this.controller?.enqueue(bytes)
    }

    unplug() {
        this.controller?.error(new DOMException('The device has been lost.', 'NetworkError'))
        this.readable = null
        this.writable = null
    }

    get isOpen() {
        return this.readable !== null
    }
}

/** navigator.serial with the ports this origin was granted; `plugIn` fires 'connect' as Chrome does. */
export function fakeSerial(granted: FakeSerialPort[], picked?: FakeSerialPort) {
    const serial = new EventTarget() as EventTarget & {
        getPorts: () => Promise<FakeSerialPort[]>
        requestPort: () => Promise<FakeSerialPort>
    }
    serial.getPorts = async () => [...granted]
    serial.requestPort = async () => {
        if (!picked) throw new DOMException('No port selected by the user.', 'NotFoundError')
        granted.push(picked)
        return picked
    }
    return {
        serial,
        plugIn: (port: FakeSerialPort) => {
            granted.push(port)
            // Chrome fires 'connect' at the port and it bubbles to navigator.serial.
            const event = new Event('connect')
            serial.dispatchEvent(Object.defineProperty(event, 'target', { value: port }))
        },
        unplug: (port: FakeSerialPort) => {
            granted.splice(granted.indexOf(port), 1)
            port.unplug()
        }
    }
}

/** The robot's side of a serial port: decodes what the app writes and answers through `reply`. */
export function serialRobot(
    port: FakeSerialPort,
    reply: (message: Message) => Message | undefined
) {
    const received: Message[] = []
    const decoder = new SerialFrameDecoder({
        onFrame: bytes => {
            const message = Message.decode(bytes)
            received.push(message)
            const answer = reply(message)
            if (answer) port.deliver(encodeSerialFrame(Message.encode(answer).finish()))
        },
        onLogLine: () => {}
    })
    port.onWrite = bytes => decoder.feed(bytes)
    return { received }
}

export const pongs = (message: Message) =>
    message.pingmsg ? Message.create({ pongmsg: {} }) : undefined

/** The fake carries only what the transport uses of a SerialPort. */
export const asPort = (port: FakeSerialPort) => port as unknown as SerialPort
