import type { ITransport, TransportHandlers } from './transport.interface'

// Nordic UART Service, matching robot_comm_ble's ble_adapter.h.
export const BLE_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e'
const BLE_CHARACTERISTIC_TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e' // robot -> client (notify)
const BLE_CHARACTERISTIC_RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e' // client -> robot (write)

/**
 * robot_comm_ble carries exactly one protobuf frame per notification and expects one per write:
 * no length prefix and no reassembly (see ble_adapter.cpp). A message therefore has to fit inside
 * the negotiated ATT payload, which is the firmware's MTU of 247 less the 3-byte ATT header.
 * Anything larger is dropped by the firmware with only a log line, so we refuse it here instead.
 */
export const BLE_MAX_FRAME_SIZE = 244

export const isBluetoothSupported = () =>
    typeof navigator !== 'undefined' && 'bluetooth' in navigator

export const createBleTransport = (handlers: TransportHandlers): ITransport => {
    let device: BluetoothDevice | undefined
    let rx: BluetoothRemoteGATTCharacteristic | undefined
    let connected = false
    // Web Bluetooth rejects overlapping GATT writes, so they are queued rather than fired in parallel.
    let writeQueue = Promise.resolve()

    const onDisconnected = () => {
        connected = false
        rx = undefined
        handlers.onClose('close')
    }

    return {
        kind: 'bluetooth',
        canAutoReconnect: false,
        maxFrameSize: BLE_MAX_FRAME_SIZE,

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

            tx.addEventListener('characteristicvaluechanged', event => {
                const value = (event.target as BluetoothRemoteGATTCharacteristic).value
                if (!value) return
                const frame = new Uint8Array(value.byteLength)
                frame.set(new Uint8Array(value.buffer, value.byteOffset, value.byteLength))
                handlers.onData(frame.buffer)
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

        send: data => {
            if (!rx || !connected) return
            if (data.length > BLE_MAX_FRAME_SIZE) {
                console.warn(
                    `Dropping ${data.length} byte message: BLE frames are capped at ${BLE_MAX_FRAME_SIZE} bytes. Use a WiFi connection for this.`
                )
                return
            }
            const characteristic = rx
            // Copied because the write is queued: the caller is free to reuse its buffer meanwhile.
            const frame = new Uint8Array(data.length)
            frame.set(data)
            writeQueue = writeQueue
                .then(() =>
                    typeof characteristic.writeValueWithoutResponse === 'function' ?
                        characteristic.writeValueWithoutResponse(frame)
                    :   characteristic.writeValue(frame)
                )
                .catch(error => console.error('BLE write failed:', error))
        }
    }
}
