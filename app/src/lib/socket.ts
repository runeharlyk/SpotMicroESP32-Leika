import { writable, type Writable } from 'svelte/store';

export type WebSocketStatus = 'OPEN' | 'CONNECTING' | 'CLOSED'

export const isConnected = writable(false)

export const status:Writable<WebSocketStatus> = writable('CLOSED')

export const socket = writable(null)

export const connect = (url:string) => {
    status.set('CONNECTING')
    let _socket = new WebSocket(url);
    _socket.onopen = _connected;
    _socket.onclose = _disconnected;
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

