import type { ITransport, TransportHandlers } from './transport.interface'

// A WebSocket has no event for its send buffer emptying, so a busy one is looked at this often until it has.
const DRAIN_POLL_MS = 20

export const createWebSocketTransport = (
    url: string | URL,
    handlers: TransportHandlers
): ITransport => {
    let ws: WebSocket | undefined
    let drainCheck: ReturnType<typeof setTimeout> | undefined

    const busy = () => (ws?.bufferedAmount ?? 0) > 0

    const watchDrain = () => {
        if (drainCheck) return
        const check = () => {
            if (busy() && ws?.readyState === WebSocket.OPEN) {
                drainCheck = setTimeout(check, DRAIN_POLL_MS)
                return
            }
            drainCheck = undefined
            handlers.onDrained?.()
        }
        drainCheck = setTimeout(check, DRAIN_POLL_MS)
    }

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

        close: () => {
            clearTimeout(drainCheck)
            drainCheck = undefined
            ws?.close()
        },

        isConnected: () => ws?.readyState === WebSocket.OPEN,

        busy,

        send: data => {
            ws?.send(data)
            if (busy()) watchDrain()
        }
    }
}
