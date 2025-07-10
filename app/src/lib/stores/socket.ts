import { writable } from 'svelte/store';
import msgpack from 'msgpack-lite';

const socketEvents = ['open', 'close', 'error', 'message', 'unresponsive'] as const;
type SocketEvent = (typeof socketEvents)[number];

type SocketMessage = [number, string?, unknown?];

let useBinary = false;

const decodeMessage = (data: string | ArrayBuffer): SocketMessage | null => {
  useBinary = data instanceof ArrayBuffer;

  try {
    if (useBinary) {
      return msgpack.decode(new Uint8Array(data as ArrayBuffer));
    }
    return JSON.parse(data as string);
  } catch (error) {
    console.error(`Could not decode data: ${data} - ${error}`);
  }
  return null;
};

const encodeMessage = (data: unknown) => {
  try {
    return useBinary ? msgpack.encode(data) : JSON.stringify(data);
  } catch (error) {
    console.error(`Could not encode data: ${data} - ${error}`);
  }
};

function createWebSocket() {
  const listeners = new Map<string, Set<(data?: unknown) => void>>();
  const { subscribe, set } = writable(false);
  const reconnectTimeoutTime = 5000;
  let unresponsiveTimeoutId: ReturnType<typeof setTimeout>;
  let reconnectTimeoutId: ReturnType<typeof setTimeout>;
  let ws: WebSocket;
  let socketUrl: string | URL;

  function init(url: string | URL) {
    socketUrl = url;
    connect();
  }

  function disconnect(reason: SocketEvent, event?: Event) {
    ws.close();
    set(false);
    clearTimeout(unresponsiveTimeoutId);
    clearTimeout(reconnectTimeoutId);
    listeners.get(reason)?.forEach(listener => listener(event));
    reconnectTimeoutId = setTimeout(connect, reconnectTimeoutTime);
  }

  function connect() {
    ws = new WebSocket(socketUrl);
    ws.binaryType = 'arraybuffer';
    ws.onopen = ev => {
      ping();
      useBinary = true;
      ping();
      set(true);
      clearTimeout(reconnectTimeoutId);
      listeners.get('open')?.forEach(listener => listener(ev));
      for (const event of listeners.keys()) {
        if (socketEvents.includes(event as SocketEvent)) continue;
        subscribeToEvent(event);
      }
    };
    ws.onmessage = frame => {
      resetUnresponsiveCheck();
      const message = decodeMessage(frame.data);
      if (!message) return;
      const [_, event, payload = undefined] = message;
      if (event) listeners.get(event)?.forEach(listener => listener(payload));
    };
    ws.onerror = ev => disconnect('error', ev);
    ws.onclose = ev => disconnect('close', ev);
  }

  function unsubscribe(event: string, listener?: (data: unknown) => void) {
    const eventListeners = listeners.get(event);
    if (!eventListeners) return;

    if (!eventListeners.size) {
      unsubscribeToEvent(event);
    }
    if (listener) {
      eventListeners?.delete(listener);
    } else {
      listeners.delete(event);
    }
  }

  function resetUnresponsiveCheck() {
    clearTimeout(unresponsiveTimeoutId);
    unresponsiveTimeoutId = setTimeout(() => disconnect('unresponsive'), reconnectTimeoutTime);
  }

  function sendEvent(event: string, data: unknown) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    send([2, event, data]);
  }

  function unsubscribeToEvent(event: string) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    send([1, event]);
  }

  function subscribeToEvent(event: string) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    send([0, event]);
  }

  function send(data: unknown) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    const serialized = encodeMessage(data);
    if (!serialized) {
      console.error('Could not serialize data:', data);
      return;
    }
    ws.send(serialized);
  }

  function ping() {
    const serialized = encodeMessage([4]);
    if (!serialized) {
      console.error('Could not serialize message');
      return;
    }
    ws.send(serialized);
  }

  return {
    subscribe,
    sendEvent,
    init,
    on: <T>(event: string, listener: (data: T) => void): (() => void) => {
      let eventListeners = listeners.get(event);
      if (!eventListeners) {
        if (!socketEvents.includes(event as SocketEvent)) {
          subscribeToEvent(event);
        }
        eventListeners = new Set();
        listeners.set(event, eventListeners);
      }
      eventListeners.add(listener as (data: unknown) => void);

      return () => {
        unsubscribe(event, listener as (data: unknown) => void);
      };
    },
    off: <T>(event: string, listener?: (data: T) => void) => {
      unsubscribe(event, listener as (data: unknown) => void);
    },
  };
}

export const socket = createWebSocket();
