import type { ITransport, TransportHandlers } from './transport.interface'
import { BLE_MAX_OUTGOING, BleFrameDecoder, encodeBleFrame } from './ble-framing'

// Nordic UART Service, as the firmware's nimble_link.cpp serves it.
export const BLE_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e'
const BLE_CHARACTERISTIC_TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e' // robot -> client (notify)
const BLE_CHARACTERISTIC_RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e' // client -> robot (write)

export const isBluetoothSupported = () =>
    typeof navigator !== 'undefined' && 'bluetooth' in navigator

export const createBleTransport = (handlers: TransportHandlers): ITransport => {
    let device: BluetoothDevice | undefined
    let rx: BluetoothRemoteGATTCharacteristic | undefined
    let connected = false
    // Web Bluetooth rejects overlapping GATT writes, so they are queued rather than fired in parallel.
    let writeQueue = Promise.resolve()
    let writing = 0

    const onDisconnected = () => {
        connected = false
        rx = undefined
        handlers.onClose('close')
    }

    return {
        kind: 'bluetooth',
        canAutoReconnect: false,
        maxFrameSize: BLE_MAX_OUTGOING,

        connect: async () => {
            if (!isBluetoothSupported()) {
                throw new Error('Web Bluetooth is unavailable. It needs Chrome over HTTPS.')
            }

            device = await navigator.bluetooth.requestDevice({
                filters: [{ services: [BLE_SERVICE_UUID] }]
            })
            if (!device.gatt) throw new Error('The selected device exposes no GATT server')

            const server = await device.gatt.connect()
            const service = await server.getPrimaryService(BLE_SERVICE_UUID)
            const tx = await service.getCharacteristic(BLE_CHARACTERISTIC_TX)
            rx = await service.getCharacteristic(BLE_CHARACTERISTIC_RX)

            // A connection starts with no partial message, whatever the previous one left.
            const frames = new BleFrameDecoder(message => {
                try {
                    handlers.onData(message.buffer as ArrayBuffer)
                } catch (error) {
                    // Thrown on, it would end the notification's handling and lose the messages after it.
                    console.error('A BLE message could not be handled:', error)
                }
            })
            tx.addEventListener('characteristicvaluechanged', event => {
                const value = (event.target as BluetoothRemoteGATTCharacteristic).value
                if (!value) return
                frames.feed(new Uint8Array(value.buffer, value.byteOffset, value.byteLength))
            })
            await tx.startNotifications()
            device.addEventListener('gattserverdisconnected', onDisconnected)

            connected = true
            handlers.onOpen()
        },

        close: () => {
            device?.removeEventListener('gattserverdisconnected', onDisconnected)
            if (device?.gatt?.connected) device.gatt.disconnect()
            onDisconnected()
        },

        isConnected: () => connected && device?.gatt?.connected === true,

        busy: () => writing > 0,

        send: data => {
            if (!rx || !connected) return
            if (data.length > BLE_MAX_OUTGOING) {
                console.warn(
                    `Dropping ${data.length} byte message: the robot takes at most ${BLE_MAX_OUTGOING} bytes over BLE. Use a WiFi connection for this.`
                )
                return
            }
            const characteristic = rx
            // The chunks are copies, so the caller is free to reuse its buffer while they wait in the queue.
            writing++
            for (const chunk of encodeBleFrame(data)) {
                writeQueue = writeQueue
                    .then(() =>
                        typeof characteristic.writeValueWithoutResponse === 'function' ?
                            characteristic.writeValueWithoutResponse(chunk)
                        :   characteristic.writeValue(chunk)
                    )
                    .catch(error => console.error('BLE write failed:', error))
            }
            writeQueue = writeQueue.then(() => {
                if (--writing === 0) handlers.onDrained?.()
            })
        }
    }
}
