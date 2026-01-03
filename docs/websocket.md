# WebSocket API

The ESP32 exposes a WebSocket endpoint at `/api/ws` for real-time bidirectional communication using Protocol Buffers (protobuf).

## Connection

Connect to the WebSocket at:

```
ws://<device-ip>/api/ws
```

All messages are binary-encoded protobuf `Message` wrappers defined in `platform_shared/message.proto`.

## Message Flow

The WebSocket supports three communication patterns:

1. **Client to Server**: Commands like controller input, mode changes, servo control
2. **Server to Client**: Periodic data broadcasts like IMU, system metrics, RSSI, servo angles
3. **Request-Response**: Use `socket.request()` for operations requiring a response

## Example: Sending Controller Input

```typescript
import { Message, ControllerData } from "./proto/message";

const input: ControllerData = {
  left: { x: 0.5, y: 0.0 },
  right: { x: 0.0, y: 0.0 },
  height: 0.1,
  speed: 1.0,
  s1: 0.0,
};

const message = Message.encode({ ControllerData: input }).finish();
socket.send(message);
```

## Example: Request-Response

```typescript
const response = await socket.request({ imuCalibrateExecute: {} });
const result = response.imuCalibrateData;
```

See `platform_shared/message.proto` for all available message types and their definitions.
