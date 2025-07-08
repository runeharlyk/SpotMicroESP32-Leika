import { writable } from 'svelte/store'

const socketEvents = ['open', 'close', 'error', 'message', 'unresponsive'] as const
type SocketEvent = (typeof socketEvents)[number]

export enum Topics {
  imu = 0,
  mode = 1,
  command = 2,
  servo = 3,
  input = 4,
  angles = 5,
  position = 6
}

function createWebSocket() {
  let listeners = new Map<string | Topics, Set<(data?: unknown) => void>>()
  const { subscribe, set } = writable(false)
  const reconnectTimeoutTime = 5000
  let unresponsiveTimeoutId: number
  let reconnectTimeoutId: number
  let ws: WebSocket
  let socketUrl: string | URL

  function init(url: string | URL) {
    socketUrl = url
    connect()
  }

  function disconnect(reason: SocketEvent, event?: Event) {
    ws.close()
    set(false)
    clearTimeout(unresponsiveTimeoutId)
    clearTimeout(reconnectTimeoutId)
    listeners.get(reason)?.forEach(listener => listener(event))
    reconnectTimeoutId = setTimeout(connect, reconnectTimeoutTime)
  }

  function connect() {
    ws = new WebSocket(socketUrl)
    ws.onopen = ev => {
      set(true)
      clearTimeout(reconnectTimeoutId)
      listeners.get('open')?.forEach(listener => listener(ev))
      for (const event of listeners.keys()) {
        if (socketEvents.includes(event as SocketEvent)) continue
        subscribeToEvent(event as unknown as Topics)
      }
    }
    ws.onmessage = message => {
      resetUnresponsiveCheck()
      let data = message.data
      if (data instanceof ArrayBuffer) {
        listeners.get('binary')?.forEach(listener => listener(data))
        return
      }
      data = data.substring(1)

      if (!data) return

      let event = data.substring(data.indexOf('/') + 1, data.indexOf('['))
      let payload = data.substring(data.indexOf('[') + 1, data.lastIndexOf(']'))

      try {
        payload = JSON.parse(payload)
      } catch (error) {}
      if (event) listeners.get(event)?.forEach(listener => listener(payload))
    }
    ws.onerror = ev => disconnect('error', ev)
    ws.onclose = ev => disconnect('close', ev)
  }

  function unsubscribe(event: Topics, listener?: (data: any) => void) {
    let eventListeners = listeners.get(event)
    if (!eventListeners) return

    if (!eventListeners.size) {
      unsubscribeToEvent(event)
    }
    if (listener) {
      eventListeners?.delete(listener)
    } else {
      listeners.delete(event)
    }
  }

  function resetUnresponsiveCheck() {
    clearTimeout(unresponsiveTimeoutId)
    unresponsiveTimeoutId = setTimeout(() => disconnect('unresponsive'), reconnectTimeoutTime)
  }

  function sendEvent(event: Topics, data: unknown) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return
    ws.send(JSON.stringify([2, event, data]))
  }

  function unsubscribeToEvent(event: Topics) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return
    ws.send(`[1,${event}]`)
  }

  function subscribeToEvent(event: Topics) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return
    ws.send(`[0,${event}]`)
  }

  return {
    subscribe,
    sendEvent,
    init,
    on: <T>(event: Topics | SocketEvent, listener: (data: T) => void): (() => void) => {
      let eventListeners = listeners.get(event)
      if (!eventListeners) {
        if (!socketEvents.includes(event)) {
          subscribeToEvent(event)
        }
        eventListeners = new Set()
        listeners.set(event, eventListeners)
      }
      eventListeners.add(listener as (data: any) => void)

      return () => {
        unsubscribe(event, listener)
      }
    },
    off: (event: Topics, listener?: (data: any) => void) => {
      unsubscribe(event, listener)
    }
  }
}

export const socket = createWebSocket()
