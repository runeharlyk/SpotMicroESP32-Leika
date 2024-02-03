import { writable, type Writable } from 'svelte/store';

export type WebSocketStatus = 'OPEN' | 'CONNECTING' | 'CLOSED'

export const isConnected = writable(false)

export const angles = writable(new Int16Array(12).fill(0))
export const log = writable([])
export const battery = writable({})
export const mpu = writable({heading:0})
export const distances = writable({})
export const settings = writable({})
export const systemInfo = writable({})

export const dataBuffer = writable(new Float32Array(13))

export const servoBuffer:Writable<Int16Array|number[]> = writable(new Int16Array(12))

export const data = writable();

export const status:Writable<WebSocketStatus> = writable('CLOSED')

export const socket:Writable<WebSocket> = writable(null)

export const connect = (url:string) => {
    status.set('CONNECTING')
    let _socket = new WebSocket(url);
    _socket.binaryType = "arraybuffer";
    _socket.onopen = _connected;
    _socket.onclose = _disconnected;
    _socket.onmessage = _message;
    socket.set(_socket)

    servoBuffer.subscribe(data => {
        if(_socket.readyState !== 1) return
        const buffer = []
        buffer[0] = 1
        buffer.push(...data)
        _socket.send(new Int16Array(buffer))
    })
}

const _connected = () => {
    status.set('OPEN')
    isConnected.set(true)
}

const _disconnected = () => {
    status.set('CLOSED')
    isConnected.set(false)
}

const _message = (event:any) => {    
    if (event.data instanceof ArrayBuffer) {
        let buffer = new Int8Array(event.data);
        if(buffer.length === 44) {
            dataBuffer.set(new Float32Array(buffer.buffer) )
        }
    } else {
        let data = event.data
        try {
            data = JSON.parse(event.data)
        } catch (error) {
            console.warn(error)
        }
        switch (data.type) {
            case "angles":
                angles.set(data.angles)
                break
            case "mpu":
                mpu.set(data.mpu)
                break
            case "distances":
                distances.set(data.distances)
                break
             case "battery":
                battery.set(data.battery)
                break    
        }
    }
}