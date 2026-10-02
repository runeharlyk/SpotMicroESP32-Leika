# Adding New Proto Files

## Step-by-Step Guide

### 1. Create the new .proto file

Create `platform_shared/myfeature.proto`:

```protobuf
syntax = "proto3";

package socket_message;

message MyFeatureRequest {
    string name = 1;
}

message MyFeatureResponse {
    bool success = 1;
    string error = 2;
}
```

### 2. Create the .options file (for nanopb size constraints)

Create `platform_shared/myfeature.options`:

```
socket_message.MyFeatureRequest.name max_size:64
socket_message.MyFeatureResponse.error max_size:128
```

A string, bytes or repeated field without a size option becomes a nanopb callback field.
Give every such field `max_size`, `max_count` or `type:FT_POINTER`, as the existing `.options` files do.

### 3. Import in message.proto

Add the import at the top of `platform_shared/message.proto`:

```protobuf
import "myfeature.proto";
```

### 4. Add to Message oneof (if needed for streaming/pub-sub)

If your message needs to be sent directly (not via correlation request/response), add it to the `Message` oneof in `message.proto`:

```protobuf
message Message {
    oneof message {
        // ... existing fields ...
        MyFeatureRequest my_feature_request = 300;  // Pick unused tag number
        MyFeatureResponse my_feature_response = 301;
    }
}
```

The `Message` message reserves the tags it retired (120, 121, 140, 180, 181, 190, 191, 200 and 240); never reuse them.
A client subscribes to a message by sending the tag it has in this oneof.

### 5. Add to CorrelationRequest/Response (if using request/response pattern)

For request/response messages, add to the correlation oneofs in `message.proto`:

```protobuf
message CorrelationRequest {
    oneof request {
        // ... existing fields ...
        MyFeatureRequest my_feature_request = 210;  // Pick unused tag number
    }
}

message CorrelationResponse {
    oneof response {
        // ... existing fields ...
        MyFeatureResponse my_feature_response = 200;  // Pick unused tag number
    }
}
```

The request and response oneofs number their fields independently.
A type from a file with another package is written with its package, for example `api.WifiSettings`.

The firmware answers a correlation request through the `correlationHandlers` map in `esp32/src/main.cpp`.
Add an entry keyed by `socket_message_CorrelationRequest_my_feature_request_tag`.
Its handler sets `res.which_response` and fills the response; `res.status_code` is already 200.

### 6. Update compile scripts

**ESP32 (esp32/scripts/compile_protos.py):**

```python
proto_files = [proto_dir / "filesystem.proto", proto_dir / "message.proto", proto_dir / "api.proto", proto_dir / "myfeature.proto"]
```

**TypeScript (app/scripts/compile_protos.js):**

```javascript
const protoFiles = ['filesystem.proto', 'message.proto', 'api.proto', 'myfeature.proto']
```

**Python simulation (simulation/gen_protos.py):**

```python
files = [os.path.join(PROTOS, name) for name in ("filesystem.proto", "api.proto", "message.proto", "myfeature.proto")]
```

### 7. Update socket.ts (for TypeScript - only if added to Message oneof)

If you added messages to the `Message` oneof, import the protoMetadata in `app/src/lib/stores/socket.ts`:

```typescript
import { protoMetadata as myfeatureProtoMetadata } from '$lib/platform_shared/myfeature'

// Add to combinedReferences
const combinedReferences: Record<string, MessageFns<unknown>> = {
    ...protoMetadata.references,
    ...filesystemProtoMetadata.references,
    ...myfeatureProtoMetadata.references  // Add this
}
```

A missing entry silently drops the type from the tag maps used by `socket.on` and `socket.emit`.
`api.proto` has no entry because none of its types is a direct member of `Message`.

### 8. Add MessageTraits (for ESP32 - only if added to Message oneof)

If you added messages to the `Message` oneof, add traits in `esp32/include/communication/proto_helpers.h`:

```cpp
// Before #undef DEFINE_MESSAGE_TRAITS
DEFINE_MESSAGE_TRAITS(MyFeatureRequest, my_feature_request)
DEFINE_MESSAGE_TRAITS(MyFeatureResponse, my_feature_response)
```

Traits let the firmware register a handler with `wsSocket.on<T>()` and send with `wsSocket.emit()`.

### 9. Build and test

```bash
# ESP32 (pre_build.py runs compile_protos.py first)
pio run

# TypeScript
cd app && pnpm proto

# Python simulation
cd simulation && uv run python gen_protos.py
```

The generated sources are not tracked: `esp32/src/platform_shared/`, `app/src/lib/platform_shared/` and `simulation/src/proto/`.
The firmware compiles the nanopb output because `esp32/src/CMakeLists.txt` lists `platform_shared` as a source directory.
Nanopb comes from the `submodules/nanopb` submodule; initialise it with `git submodule update --init --recursive`.

## Quick Reference

| File | Purpose |
|------|---------|
| `platform_shared/*.proto` | Protocol definitions |
| `platform_shared/*.options` | Nanopb size constraints |
| `esp32/scripts/compile_protos.py` | ESP32 proto compilation |
| `app/scripts/compile_protos.js` | TypeScript proto compilation (`pnpm proto`) |
| `simulation/gen_protos.py` | Python proto compilation |
| `app/src/lib/stores/socket.ts` | Tag mapping for socket.on/emit |
| `esp32/include/communication/proto_helpers.h` | MessageTraits for ESP32 emit |
| `esp32/src/main.cpp` | Handlers for incoming messages and correlation requests |

## Notes

- Messages in `CorrelationRequest/Response` don't need MessageTraits or socket.ts updates, but the firmware needs a `correlationHandlers` entry
- Messages in `Message` oneof (for streaming/pub-sub) need both
- Use `package socket_message;` for messages that become members of `Message`, because `DEFINE_MESSAGE_TRAITS` assumes the `socket_message_` prefix; `api.proto` uses `package api;` and is reached only through the correlation oneofs
- Tag numbers must be unique within the message or oneof that holds them
