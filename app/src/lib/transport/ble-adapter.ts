import type { ITransport, TransportHandlers } from './transport.interface'
import { BLE_MAX_OUTGOING, BleReassembler, encodeBleFrame } from './ble-framing'

// Nordic UART Service, as the firmware's nimble_link.cpp serves it.
export const BLE_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e'
const BLE_CHARACTERISTIC_TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e' // robot -> client (notify)
const BLE_CHARACTERISTIC_RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e' // client -> robot (write)

/** Reconnects tried after a link the user did not end drops, before the link stays down. */
export const BLE_RECONNECT_ATTEMPTS = 5

export const isBluetoothSupported = () =>
    typeof navigator !== 'undefined' && 'bluetooth' in navigator

export type BleTransport = ITransport & { readonly device: BluetoothDevice | undefined }

/**
 * The robot's protobuf messages over the Nordic UART Service. `connect` opens `chosen`, or asks the
 * user for a device when none is given, which needs a user gesture; reconnecting a device chosen
 * before needs none.
 */
export const createBleTransport = (
    handlers: TransportHandlers,
    chosen?: BluetoothDevice
): BleTransport => {
    let device = chosen
    let tx: BluetoothRemoteGATTCharacteristic | undefined
    let rx: BluetoothRemoteGATTCharacteristic | undefined
    let connected = false
    // Web Bluetooth rejects overlapping GATT writes, so they are queued rather than fired in parallel.
    let writeQueue = Promise.resolve()
    let writing = 0
    const reassembler = new BleReassembler()

    const release = () => {
        tx?.removeEventListener('characteristicvaluechanged', onNotification)
        device?.removeEventListener('gattserverdisconnected', onDisconnected)
        if (device?.gatt?.connected) device.gatt.disconnect()
        connected = false
        tx = undefined
        rx = undefined
    }

    const onDisconnected = () => {
        release()
        handlers.onClose('close')
    }

    // A stream out of step stays so for the rest of the connection. A new connection starts it
    // clean, and the socket subscribes again on it, which brings every stream back.
    const resync = () => {
        console.warn('Bluetooth stream out of step; reconnecting to resync')
        release()
        handlers.onClose('error', new Error('The Bluetooth stream fell out of step'))
    }

    const onNotification = (event: Event) => {
        const value = (event.target as BluetoothRemoteGATTCharacteristic).value
        if (!value) return
        const message = reassembler.push(
            new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
        )
        if (message === 'partial') return
        if (message === 'out-of-step') return resync()
        try {
            handlers.onData(message.buffer)
        } catch (error) {
            // Thrown on, it would end the notification's handling and lose the messages after it.
            console.error('A BLE message could not be handled:', error)
        }
    }

    return {
        kind: 'bluetooth',
        get canAutoReconnect() {
            return device !== undefined
        },
        maxFrameSize: BLE_MAX_OUTGOING,

        get device() {
            return device
        },

        connect: async () => {
            if (!isBluetoothSupported()) {
                throw new Error('Web Bluetooth is unavailable. It needs Chrome over HTTPS.')
            }

            device ??= await navigator.bluetooth.requestDevice({
                filters: [{ services: [BLE_SERVICE_UUID] }]
            })
            if (!device.gatt) throw new Error('The selected device exposes no GATT server')

            try {
                const server = await device.gatt.connect()
                const service = await server.getPrimaryService(BLE_SERVICE_UUID)
                tx = await service.getCharacteristic(BLE_CHARACTERISTIC_TX)
                rx = await service.getCharacteristic(BLE_CHARACTERISTIC_RX)
                tx.addEventListener('characteristicvaluechanged', onNotification)
                await tx.startNotifications()
                device.addEventListener('gattserverdisconnected', onDisconnected)
            } catch (error) {
                release()
                throw error
            }

            connected = true
            handlers.onOpen()
        },

        close: () => {
            release()
            handlers.onClose('close')
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
