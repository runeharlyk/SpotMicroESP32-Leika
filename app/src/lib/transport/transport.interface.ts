export type TransportKind = 'websocket' | 'bluetooth'

export type TransportCloseReason = 'close' | 'error'

export interface TransportHandlers {
    onOpen: () => void
    onData: (data: ArrayBuffer) => void
    onClose: (reason: TransportCloseReason, event?: unknown) => void
}

export interface ITransport {
    readonly kind: TransportKind
    /** WiFi can silently redial; BLE pairing needs a user gesture, so the caller must not retry. */
    readonly canAutoReconnect: boolean
    /** Largest frame the link carries, or null when it is effectively unlimited. */
    readonly maxFrameSize: number | null
    connect: () => Promise<void>
    close: () => void
    isConnected: () => boolean
    send: (data: Uint8Array) => void
}
