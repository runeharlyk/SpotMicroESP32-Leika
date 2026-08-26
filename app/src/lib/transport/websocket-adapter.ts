import type { ITransport, TransportHandlers } from './transport.interface'

export const createWebSocketTransport = (
    url: string | URL,
    handlers: TransportHandlers
): ITransport => {
    let ws: WebSocket | undefined

    return {
        kind: 'websocket',
        canAutoReconnect: true,
        maxFrameSize: null,

        connect: async () => {
            ws = new WebSocket(url)
            ws.binaryType = 'arraybuffer'
            ws.onopen = () => handlers.onOpen()
            ws.onmessage = frame => handlers.onData(frame.data)
            ws.onerror = event => handlers.onClose('error', event)
            ws.onclose = event => handlers.onClose('close', event)
        },

        close: () => ws?.close(),

        isConnected: () => ws?.readyState === WebSocket.OPEN,

        send: data => ws?.send(data)
    }
}
