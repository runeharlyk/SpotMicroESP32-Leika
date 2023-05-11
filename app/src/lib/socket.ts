import { writable, type Writable } from 'svelte/store';

export type WebSocketStatus = 'OPEN' | 'CONNECTING' | 'CLOSED'

export const isConnected = writable(false)

export const data = writable(new Float32Array(13))

export const status:Writable<WebSocketStatus> = writable('CLOSED')

export const socket = writable(null)

export const connect = (url:string) => {
    status.set('CONNECTING')
    let _socket = new WebSocket(url);
    _socket.binaryType = "arraybuffer";
    _socket.onopen = _connected;
    _socket.onclose = _disconnected;
    _socket.onmessage = _message;
    socket.set(_socket)
}

const _connected = () => {
    status.set('OPEN')
    isConnected.set(true)
}

const _disconnected = () => {
    status.set('CLOSED')
    isConnected.set(false)
}

const _message = (event) => {    
    if (event.data instanceof ArrayBuffer) {
        let buffer = new Uint8Array(event.data);
        data.set(new Float32Array(buffer.buffer));
    }
}